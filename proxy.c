/**
 * Proxy Lab
 * Author: Silun Wang
 * Andrew ID: silunw
 * Date: 08-01-2015
 */
#include "csapp.h"
#include "cache.h"


/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
//static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
//static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";

typedef struct {
	/* file descriptor */
	int fd;
	struct sockaddr_storage socket_addr;
} thread_args;

/* total cache size */
int cache_size = 0;

/* the head of cache list */
struct cache_block* head;

/* the lock of the cache list.
   To add or delete a list node, you need to acquire this lock first.
*/
sem_t list_lock;

void doit(int fd);
int parse_uri(char *uri, char *hostname, char* port, char *filename);
void *thread (void *vargp);
void sigsegv_handler(int sig);

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
	init_cache(head);

	// block sigpipe
	Signal(SIGPIPE, SIG_IGN);

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
	// handle segment fault: it is sometimes weird
	Signal(SIGSEGV, sigsegv_handler);
	printf("doit %d\n", args.fd);
	doit(args.fd);
	Close(args.fd);
	printf("close %d\n", args.fd);
	Free(vargp);
	return NULL;
}

/**
 * segment fault signal handler
 */
void sigsegv_handler(int sig) {
	sio_error("Caught segment fault\n");
	pthread_exit(NULL);
	return;
}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int to_client_fd)
{
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	char filename[MAXLINE], hostname[MAXLINE];
	char port[10];
	rio_t rio_to_client;
	rio_t rio_to_server;
	int to_server_fd;

	/* Read request line and headers */
	Rio_readinitb(&rio_to_client, to_client_fd);
	Rio_readlineb(&rio_to_client, buf, MAXLINE);
	printf("%s", buf);
	sscanf(buf, "%s %s %s", method, uri, version);

	/* if not GET method, ignore it */
	if (strcasecmp(method, "GET")) {
		return;
	}

	/* Parse URI from GET request */
	parse_uri(uri, hostname, port, filename);
	sprintf(buf, "%s %s %s\r\n", "GET", filename, "HTTP/1.0");

	/* search content in cache list */
	struct cache_block* ptr = search_cache(head, uri);

	/* cache found, directly send to client */
	if (ptr) {
		// add the number of existing threads reading
		add_reading_cnt(ptr);
		// send cache to client
		Rio_writen(to_client_fd, ptr->file, ptr->size);
		// subtract the number of existing threads reading
		sub_reading_cnt(ptr);
		// update timestamp and reorder LRU list
		update_timestamp(head, ptr);

		return;
	}

	/* cache not found, connect with server */
	to_server_fd = Open_clientfd(hostname, port);
	printf("%s: %d\n", uri, to_server_fd);
	Rio_readinitb(&rio_to_server, to_server_fd);
	// send request line: GET HTTP/1.0
	Rio_writen(to_server_fd, buf, strlen(buf));
	// send host
	sprintf(buf, "Host: %s\r\n", hostname);
	Rio_writen(to_server_fd, buf, strlen(buf));

	Rio_readlineb(&rio_to_client, buf, MAXLINE);

	/* read http headers from client and write to server */
	while (strncmp(buf, "\r\n", MAXLINE) != 0) {
		if (strstr(buf, "User-Agent"))
			strncpy(buf, user_agent_hdr, MAXLINE);
		else if (strstr(buf, "Connection"))
			strncpy(buf, "Connection: close\r\n", MAXLINE);
		else if (strstr(buf, "Proxy-Connection"))
			strncpy(buf, "Proxy-Connection: close\r\n", MAXLINE);
		else if (strstr(buf, "Host")) {
			// ignore, because we already sent one
			Rio_readlineb(&rio_to_client, buf, MAXLINE);
			continue;
		}
		Rio_writen(to_server_fd, buf, strlen(buf));
		Rio_readlineb(&rio_to_client, buf, MAXLINE);
	}
	/* terminates request headers */
	Rio_writen(to_server_fd, "\r\n", 2);

	/* read http response from server and write to client */
	Rio_readlineb(&rio_to_server, buf, MAXLINE);
	int content_len = 0;
	while (strncmp(buf, "\r\n", MAXLINE) != 0) {
		char* ptr = strstr(buf, "Content-length");
		if (ptr) {
			content_len = atoi(ptr + 16);
		}
		printf("write2: %s\n", uri);
		printf("fd: %d\n", to_client_fd);
		Rio_writen(to_client_fd, buf, strlen(buf));
		Rio_readlineb(&rio_to_server, buf, MAXLINE);
	}
	/* terminates response headers */
	Rio_writen(to_client_fd, "\r\n", 2);

	int size = 0;
	int cacheit = 0;
	// shall we cache it?
	if (content_len < MAX_OBJECT_SIZE)
		cacheit = 1;

	// init a new cache block
	struct cache_block* blk = (struct cache_block*) malloc(sizeof(struct cache_block));
	init_cache(blk);
	strncpy(blk->uri, uri, MAXLINE);

	if (content_len > 0)
		blk->file = (char*) malloc(sizeof(char) * content_len);
	else
		blk->file = (char*) malloc(sizeof(char) * MAX_OBJECT_SIZE);

	memset(buf, 0, MAXLINE);
	int total_size = 0;
	char* headptr = blk->file;

	// read response contents and write to client
	while ((size = Rio_readnb(&rio_to_server, buf, MAXLINE)) > 0) {
		total_size += size;
		if (total_size > MAX_OBJECT_SIZE)
			cacheit = 0;
		if (cacheit) {
			memcpy(headptr, buf, size);
			headptr += size;
		}
		printf("write3: %s\n", uri);
		Rio_writen(to_client_fd, buf, size);
		memset(buf, 0, MAXLINE);
	}

	// add cache block
	if (cacheit) {
		blk->size = total_size;
		blk->file = (char*) realloc(blk->file, blk->size);
		// size overflow, need to evict using LRU
		if (blk->size + cache_size > MAX_CACHE_SIZE)
			evict_cache(head, blk->size);
		add_cache(head, blk);
	}
	// prevent memory leakage
	else {
		free_cache_node(blk);
	}

	Close(to_server_fd);
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

	char* p = strtok(hostname, ":");
	// get port number
	p = strtok(NULL, ":");
	if (p)
		strcpy(port, p);
	else
		strcpy(port, "80");
	file_start = host_end;
	strcpy(filename, file_start);
	return 0;
}
