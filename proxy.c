#include "csapp.h"
#include "cache.h"


/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";

typedef struct {
	int fd;	
	struct sockaddr_storage socket_addr;
} thread_args;

int cache_size = 0;
struct cache_block* head;
sem_t list_lock;

void doit(int fd);
int parse_uri(char *uri, char *hostname, char* port, char *filename);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void *thread (void *vargp);
void sigsegv_handler(int sig);

void sigsegv_handler(int sig) {
	sio_error("segment fault\n");
	return;
}

int main(int argc, char **argv)
{
	int listenfd;
	char hostname[MAXLINE], port[MAXLINE];

	/* Check command line args */
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	listenfd = Open_listenfd(argv[1]);
	Sem_init(&list_lock, 0, 1);
	// init cache's head node
	head = (struct cache_block*) malloc(sizeof(struct cache_block));
	head->size = 0;
	head->timestamp = clock();
	head->next = NULL;
	head->file = NULL;

	// block sigpipe
	Signal(SIGPIPE, SIG_IGN);
	// handle segment fault: it is sometimes weird
	Signal(SIGSEGV, sigsegv_handler);

	while (1) {

		pthread_t tid;
		// allocate space for a thread arg
		thread_args* args_ptr = (thread_args*) malloc(sizeof(thread_args));
		if (!args_ptr) {
			printf("malloc failure\n");
			continue;
		}
		socklen_t clientlen = sizeof(struct sockaddr_storage);
		printf("Preparing to connect with clients...\n");

		args_ptr->fd = accept(listenfd, (SA *)&args_ptr->socket_addr, &clientlen);
		if (args_ptr->fd < 0) {
			printf("accept failure\n");
			continue;
		}
		if (getnameinfo((SA *) &args_ptr->socket_addr, clientlen, hostname, MAXLINE, port, MAXLINE, 0) != 0) {
			printf("getnameinfo failure\n");
			continue;
		}
		printf("Accepted connection from (%s, %s)\n", hostname, port);
		if (pthread_create(&tid, NULL, thread, args_ptr) != 0) {
			printf("pthread_create error\n");
			continue;
		}
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

	// if not GET method, ignore it
	if (strcasecmp(method, "GET")) {                     
		clienterror(fd, method, "501", "Not Implemented",
		            "Tiny does not implement this method");
		return;
	}                                                    

	/* Parse URI from GET request */
	parse_uri(uri, hostname, port, filename);

	sprintf(buf, "%s %s %s\r\n", "GET", filename, "HTTP/1.0");

	struct cache_block* ptr;
	ptr = search_cache(head, uri);
	// cache found
	if (ptr) {
		add_reading_cnt(ptr);
		// send cache to client
		Rio_writen(fd, ptr->file, ptr->size);
		sub_reading_cnt(ptr);
		update_timestamp(head, ptr);
		return;
	}

	// cache not found, connect with server
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
	int cacheit = 0;
	// shall we cache it?
	if (content_len < MAX_OBJECT_SIZE)
		cacheit = 1;

	// init a new cache block
	struct cache_block* blk = (struct cache_block*) malloc(sizeof(struct cache_block));
	Sem_init(&blk->lock, 0, 1);
	blk->reading_cnt = 0;
	blk->size = content_len;
	strncpy(blk->uri, uri, MAXLINE);
	blk->timestamp = clock();
	blk->next = NULL;
	blk->file = (char*) malloc(sizeof(char) * content_len);

	// read response contents and write to client
	while (content_len > MAXLINE) {
		size = Rio_readnb(&rio_client, buf, MAXLINE);
		Rio_writen(fd, buf, size);
		if (cacheit)
			strncat(blk->file, buf, size);
		content_len -= MAXLINE;
	}
	// content is not null
	while ((size = Rio_readnb(&rio_client, buf, MAXLINE)) > 0) {
		Rio_writen(fd, buf, size);
		if (cacheit)
			strncat(blk->file, buf, size);
	}
	// add cache block
	if (cacheit) {
		// size overflow, need to evict using LRU
		if (blk->size + cache_size > MAX_CACHE_SIZE)
			evict_cache(head, blk->size);
		add_cache(head, blk);
	}
	
	Close(clientfd);
	return;
}
/* $end doit */


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
