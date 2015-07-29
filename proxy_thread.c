#include "csapp.h"



/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *hostname, char* port, char *filename);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);
void *thread (void *vargp);

typedef struct {
	/* data */
	int fd;
	struct sockaddr_storage socket_addr;
} thread_args;


int main(int argc, char **argv)
{
	int listenfd;
	char hostname[MAXLINE], port[MAXLINE];
	pthread_t tid;

	/* Check command line args */
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	listenfd = Open_listenfd(argv[1]);
	while (1) {
		thread_args* args_ptr = (thread_args*) malloc(sizeof(thread_args));
		socklen_t clientlen = sizeof(struct sockaddr_storage);
		args_ptr->fd = Accept(listenfd, (SA *)&args_ptr->socket_addr, &clientlen);
		Getnameinfo((SA *) &args_ptr->socket_addr, clientlen, hostname, MAXLINE,
		            port, MAXLINE, 0);
		printf("Accepted connection from (%s, %s)\n", hostname, port);
		Pthread_create(&tid, NULL, thread, args_ptr);
	}
}

void *thread (void *vargp) {
	thread_args args;
	args = *((thread_args *) vargp); 
	Pthread_detach(pthread_self());
	doit(args.fd);
	Close(args.fd);
	Free(vargp);
	return NULL;
}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int fd)
{
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	char filename[MAXLINE], hostname[MAXLINE];
	char port[10];
	rio_t rio;
	rio_t rio_client;
	int clientfd;

	/* Read request line and headers */
	Rio_readinitb(&rio, fd);
	if (!Rio_readlineb(&rio, buf, MAXLINE))
		return;
	printf("%s", buf);
	sscanf(buf, "%s %s %s", method, uri, version);     
	// if not GET method  
	if (strcasecmp(method, "GET")) {                     
		clienterror(fd, method, "501", "Not Implemented",
		            "Tiny does not implement this method");
		return;
	}                                                    

	/* Parse URI from GET request */
	parse_uri(uri, hostname, port, filename);

	sprintf(buf, "%s %s %s\r\n", "GET", filename, "HTTP/1.0");

	// connect with server
	clientfd = Open_clientfd(hostname, port);
	Rio_readinitb(&rio_client, clientfd);
	// send request line: GET HTTP/1.0
	Rio_writen(clientfd, buf, strlen(buf));
	// send host
	sprintf(buf, "Host: %s\r\n", hostname);
	Rio_writen(clientfd, buf, strlen(buf));
	Rio_readlineb(&rio, buf, MAXLINE);

	// read http headers from client and write to server
	while (strncmp(buf, "\r\n", MAXLINE) != 0) {

		if (strstr(buf, "User-Agent")) 
			strncpy(buf, user_agent_hdr, MAXLINE);
		else if (strstr(buf, "Accept")) 
			strncpy(buf, accept_hdr, MAXLINE);
		else if (strstr(buf, "Accept-Encoding")) 
			strncpy(buf, accept_encoding_hdr, MAXLINE);
		else if (strstr(buf, "Connection")) 
			strncpy(buf, "Connection: close\r\n", MAXLINE);
		else if (strstr(buf, "Proxy-Connection")) 
			strncpy(buf, "Proxy-Connection: close\r\n", MAXLINE);
		Rio_writen(clientfd, buf, strlen(buf));
		Rio_readlineb(&rio, buf, MAXLINE);
	}
	// terminates headers
	Rio_writen(clientfd, "\r\n", 2);

	// read http response from server and write to client
	Rio_readlineb(&rio_client, buf, MAXLINE);
	int content_len = 0;
	while (strncmp(buf, "\r\n", MAXLINE) != 0) {
		char* ptr = strstr(buf, "Content-length");
		if (ptr) {
			content_len = atoi(ptr + 16);
		}
		Rio_writen(fd, buf, strlen(buf));
		Rio_readlineb(&rio_client, buf, MAXLINE);
	}
	// terminates headers
	Rio_writen(fd, "\r\n", 2);
	int size = 0;
	// read response contents and write to client
	while (content_len > MAXLINE) {
		size = Rio_readnb(&rio_client, buf, MAXLINE);
		Rio_writen(fd, buf, size);
		content_len -= MAXLINE;
	}
	// content is not null
	while ((size = Rio_readnb(&rio_client, buf, MAXLINE)) > 0) {
		Rio_writen(fd, buf, size);
	}
	Close(clientfd);
	return;
}
/* $end doit */

/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp)
{
	char buf[MAXLINE];

	Rio_readlineb(rp, buf, MAXLINE);
	printf("%s", buf);
	while (strcmp(buf, "\r\n")) {
		Rio_readlineb(rp, buf, MAXLINE);
		printf("%s", buf);
	}
	return;
}
/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into filename, hostname and port
 */
int parse_uri(char *uri, char *hostname, char* port, char* filename)
{
	char* host_start;
	char* host_end;
	char* file_start;

	if (strncasecmp(uri, "http://", 7) != 0) {
		hostname = NULL;
		return -1;
	}
	// end of http://
	host_start = uri + 7;
	host_end = strstr(host_start, "/");
	size_t hostname_len = strlen(host_start) - strlen(host_end);
	strncpy(hostname, host_start, hostname_len);
	char* p;
	p = strtok(hostname, ":");
	// get port number
	p = strtok(NULL, ":");
	if (p)
		strcpy(port, p);
	else
		port = NULL;
	file_start = host_end;
	strcpy(filename, file_start);
	return 0;
}

/*
 * serve_static - copy a file back to the client
 */
void serve_static(int fd, char *filename, int filesize)
{
	int srcfd;
	char *srcp, filetype[MAXLINE], buf[MAXBUF];

	/* Send response headers to client */
	get_filetype(filename, filetype);       //line:netp:servestatic:getfiletype
	sprintf(buf, "HTTP/1.0 200 OK\r\n");    //line:netp:servestatic:beginserve
	sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
	sprintf(buf, "%sConnection: close\r\n", buf);
	sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
	sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
	Rio_writen(fd, buf, strlen(buf));       //line:netp:servestatic:endserve
	printf("Response headers:\n");
	printf("%s", buf);

	/* Send response body to client */
	srcfd = Open(filename, O_RDONLY, 0);    //line:netp:servestatic:open
	srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);//line:netp:servestatic:mmap
	Close(srcfd);                           //line:netp:servestatic:close
	Rio_writen(fd, srcp, filesize);         //line:netp:servestatic:write
	Munmap(srcp, filesize);                 //line:netp:servestatic:munmap
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype)
{
	if (strstr(filename, ".html"))
		strcpy(filetype, "text/html");
	else if (strstr(filename, ".gif"))
		strcpy(filetype, "image/gif");
	else if (strstr(filename, ".png"))
		strcpy(filetype, "image/png");
	else if (strstr(filename, ".jpg"))
		strcpy(filetype, "image/jpeg");
	else
		strcpy(filetype, "text/plain");
}
/* $end serve_static */

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
/* $begin serve_dynamic */
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
	char buf[MAXLINE], *emptylist[] = { NULL };

	/* Return first part of HTTP response */
	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Server: Tiny Web Server\r\n");
	Rio_writen(fd, buf, strlen(buf));

	if (Fork() == 0) { /* Child */ //line:netp:servedynamic:fork
		/* Real server would set all CGI vars here */
		setenv("QUERY_STRING", cgiargs, 1); //line:netp:servedynamic:setenv
		Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */ //line:netp:servedynamic:dup2
		Execve(filename, emptylist, environ); /* Run CGI program */ //line:netp:servedynamic:execve
	}
	Wait(NULL); /* Parent waits for and reaps child */ //line:netp:servedynamic:wait
}
/* $end serve_dynamic */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
	char buf[MAXLINE], body[MAXBUF];

	/* Build the HTTP response body */
	sprintf(body, "<html><title>Tiny Error</title>");
	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

	/* Print the HTTP response */
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */