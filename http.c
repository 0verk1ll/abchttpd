#define _GNU_SOURCE AA
#define _POSIX_C_SOURCE >= 199309L

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <alloca.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>

#include <stdint.h>
#include <stdbool.h>

#include "list.h"
#include "hash.h"
#include "subr.h"

/* this is a simple http server */

#define USER_AGENT	"abchttpd 0.0.0"
#define PORT 		3003
#define HDEFAULT 	"index.html"
#define HOST		"ia.jumpingcrab.com:3003"

#define FIELDS		32
#define FIELD_LEN	1024 
#define BLOCK_SZ 	4096
#define PATH_MAX 	4096
#define CLIENTS_MAX	32

#define PERS_TIME	15

static int done = 0;

struct http_msg{
	char * status;
	struct bucket * fields;
	char * message_body;
};

struct connection{
	int fd;
	struct http_msg * request;
	struct http_msg * response;
	char * buffer;
};

int fisset(struct bucket * field){
	if(field && field->key && field->val) return 1;
	return 0;
}

void set_status(int code, struct http_msg ** hh)
{
	if(hh == NULL) return;
	char ** s = NULL;
	s = &(*hh)->status;
	switch(code){
		case 200:
			*s = "HTTP/1.1 200 OK\r\n";
			break;
		case 404:
			*s = "HTTP/1.1 404 Not Found\r\n";
			break;
		case 500:
			*s = "HTTP/1.1 500 Internal Server Error\r\n";
			break;
		default:
			break;
	}
}

struct http_msg * hmsg_alloc(){
	struct http_msg * h = NULL;
	int rh_size = sizeof(struct http_msg);
	h = calloc(1, rh_size);
	if(!h) die("h calloc");
	h->fields = calloc(FIELDS, sizeof(struct bucket));
	if(!h->fields) die("h->fields calloc");
	ht_init(h->fields, FIELDS);
	if(hash_add("Server: ", USER_AGENT, FIELDS, &h->fields) < 0)
		die("hmsg_alloc\n");
	h->status = NULL;
	set_status(200, &h);
	h->message_body = NULL;
	return h;
}

void hmsg_free(struct http_msg * h){
	free(h->fields);
	free(h);
}

char * make_path(char * req, char * pathbuf)
{
	if(!pathbuf || !req) return NULL;
	if(getcwd(pathbuf, PATH_MAX) == NULL){
		perror("getcwd");
		return NULL;
	}
	if(strcmp(req, "/") == 0){
		readcpy(pathbuf+strlen(pathbuf), "/", 2);
		readcpy(pathbuf+strlen(pathbuf), HDEFAULT, PATH_MAX);	
	}else{
		readcpy(pathbuf+strlen(pathbuf), req, PATH_MAX);
	}
	pathbuf[PATH_MAX-1] = '\0';
	printf("path %s\n", pathbuf);
	return pathbuf;
}

char * loadresrc(char * path)
{
	int bytes_read = 0, s = 0, r = 0, bm = 1;
	char * buffer = NULL;
	s = open(path, O_RDONLY);
	if(s < 0)	/* caller check errno and send 404 if doesn't exist */
		return NULL;	
	buffer = calloc(BLOCK_SZ,1);
	if(!buffer) die("malloc");
	while((r = read(s, buffer, BLOCK_SZ)) != -1 && r != 0){ 
		bytes_read += r;
		if(bytes_read > bm*BLOCK_SZ){
			buffer = realloc(buffer, (++bm)*BLOCK_SZ);
			if(buffer == NULL){
				die("realloc");
			}
		}
	}
	close(s);
	printf("read %i bytes %s\n", bytes_read, path);
	return buffer;
}

int send_response(int fd, struct http_msg * htmsg)
{
	assert(fd && htmsg);
	size_t len = 0, slen = 0, clen = 0;
	char * status = htmsg->status;
	int fb_len = 100;
	char * field_buffer = alloca(fb_len);
	char * sep = "\r\n";

	slen = strlen(status);
	write(fd, status, slen);

	struct bucket * fptr = NULL;	
	for(int i = 0; i < FIELDS; i++){
		fptr = &htmsg->fields[i];
		if(fisset(fptr)){
			sprintf(field_buffer, "%s%s\n", fptr->key, fptr->val);
			write(fd, field_buffer, strlen(field_buffer));
		}
	}
	clen = strlen(htmsg->message_body);
	sprintf(field_buffer, "Content-Length: %zu\n", clen); 
	write(fd, field_buffer, strlen(field_buffer));
	
	write(fd, sep, strlen(sep));
	write(fd, htmsg->message_body, clen);
	return 0;
}
int send_code_200(int fd, struct http_msg * response)
{
	if(!response) return -1;
	set_status(200, &response);
	send_response(fd, response);
	return 0;
}

int send_error_500(int fd, struct http_msg * response)
{
	set_status(500, &response); response->message_body = "500 Internal Server Error";
	send_response(fd, response);
	return 0;
}
int send_error_404(int fd, struct http_msg * response)
{
	set_status(404, &response);
	response->message_body = "404 Not Found";
	send_response(fd, response);
	return 0;
}

/* 
 * fills out dest structure from buffer containing a request 
 *
 */
int parse_request(char * buffer, struct http_msg * dest)
{
	char ** tokens = split(buffer, "\r\n", FIELDS+1);
	char ** lntoks = NULL;
	int i = 0;
	for(i = 0; tokens[i] != '\0'; i++){
		lntoks = split(tokens[i], " ", 2);
		if(lntoks[0] && lntoks[1]){
			hash_add(lntoks[0], lntoks[1], FIELDS, &dest->fields);
		}
		if(lntoks) free(lntoks);
	}
	if(i > 1){
		dest->message_body = tokens[i-1];
	}
	free(tokens);
	return 0;
}

int handle_get(int fd, char * resource, struct connection * conn)
{
	assert(resource != NULL && conn != NULL);
	char * p = NULL, * pathbuf = NULL;
	struct http_msg ** htpr = &conn->response;

	pathbuf = alloca(PATH_MAX);
	char * pb = make_path(resource, pathbuf);
	p = loadresrc(pb);
	if(p){
		(*htpr)->message_body = p;
		send_code_200(fd, conn->response);
		free(p);
	}else{
		(*htpr)->message_body = "404";
		send_error_404(fd, conn->response);
	}
	return 0;
}
int handle_post(int fd, char * action, struct connection * conn)
{
	int pfds[2];
	pid_t forked;
	struct http_msg * h = conn->response;
	char * decoded_body;
	printf("action: %s\n", action);

	assert(conn->request->message_body != NULL);
	int mblen = strlen(conn->request->message_body);
	if(mblen <= 0){
		exit(EXIT_FAILURE);
	}
/*	decoded_body = calloc(strlen(conn->request->message_body)+1, 1);
	if(!decoded_body) die("calloc");
	percdec(decoded_body, conn->request->message_body, mblen);
	printf("%s\n", conn->request->message_body);
	printf("%s\n", decoded_body);
*/
	int p = pipe(pfds);
	if(p < 0){
		perror("pipe");
		return -1;
	}
	forked = fork();
	if(forked == -1){
		perror("fork");
		h->message_body = "error";
		send_error_500(fd, h);
	}else if(forked > 0){
		/* parent */
		close(pfds[1]);
		int buffer_size = 1024;
		int r = 0;
		char * buffer = alloca(buffer_size);
		if((r = read(pfds[0], buffer, buffer_size)) < 0){
			perror("forked_parent_read");
			buffer = "forked_parent_read";
		}
		buffer[r-1] = '\0';
		wait(NULL);
		h->message_body = buffer;
		send_code_200(fd, h);
	}else if(forked == 0){
		/* child */
		close(0);
		if(dup2(pfds[1], 1) < 0){
			perror("dup2");
			exit(EXIT_FAILURE);
		}
		char * fn = "post.py";
		char * myargv[] = {NULL};
		char * myenvp[] = {conn->request->message_body, NULL};
		execve(fn, myargv, myenvp);
		perror("execve\n");
		exit(EXIT_FAILURE);
	}
//	free(decoded_body);
	return 0;
}
int handle_request(int fd, struct connection * conn)
{
	char * buf = alloca(1024);
	if(!buf) die("allocate");
	memset(buf, 0, 1024);
	int r = 0;
re:
	r = read(fd, buf, 1024);
	if(r <= 0){
		if(errno == EINTR) goto re; 
		if(r == 0 && close(fd) < 0){
			perror("close");
			return -1;
		}
		perror("read");
		return -1;
	}
	buf[r] = '\0';
	if(parse_request(buf, conn->request) < 0){
		printf("error");
		return -1;
	}
	char * v = NULL;
	int (*method)(int, char *, struct connection *);	
	if((v = hash_getval("GET", FIELDS, conn->request->fields))!=NULL){
		method = handle_get;
	}else
	if((v = hash_getval("POST", FIELDS, conn->request->fields))!=NULL){
		method = handle_post;
	}else{
		printf("Weird request\n");
		return -1;
	}
	method(fd, v, conn);
	return 0;
}

/* signal handler */
void sigint_handler(){
	printf("sigint_handler\n");
	done = 1;
}

int main()
{
	int sockfd;
	struct sockaddr_in localsa, clientsa;
	socklen_t sa_size = sizeof(struct sockaddr);
	struct pollfd pfds[CLIENTS_MAX];
	int pfd_count = 0;
	int pfd_last = 0;	/* if a fd is removed from middle */
	
	struct connection http_conn;
	struct http_msg * request = hmsg_alloc();
	struct http_msg * response = hmsg_alloc();

	http_conn.request = request;
	http_conn.response = response;
	
	struct sigaction sigact;
	
	sigact.sa_handler = sigint_handler;
	sigaction(2, &sigact, NULL);

	/* setup network, listen for connections */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0){
		die("socket");
	}
	memset(&localsa, 0, sa_size);
	memset(&clientsa, 0, sa_size);
	
	localsa.sin_family = AF_INET;	
	localsa.sin_port = htons(PORT);
	localsa.sin_addr.s_addr = INADDR_ANY;	/* change later */
	if(bind(sockfd, (struct sockaddr*)&localsa, sa_size)<0){
		die("Failed to bind to port");
	}
	/* reuse the socket */
	int optval = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	/* wait for request */
	if(listen(sockfd, 1) < 0){
		die("listen");
	}
	
	for(int i = 0; i < CLIENTS_MAX; i++){
		pfds[i].fd = -1;
		pfds[i].events = POLLIN;
		pfds[i].revents = 0;
	}	
	
	pfds[0].fd = sockfd;
	++pfd_count;
	
	struct pollfd * pf = NULL;
	int acc = 0;
	while(done == 0){
	poll(pfds, CLIENTS_MAX,-1); 
	
	for(int i = 0; i < pfd_count; i++){
		pf = &pfds[i];
		switch(pf->revents){
		case POLLIN:
			if(i != 0) /* new client connected */
			printf("POLLIN - client[%i]\n", i);
			if(pf->fd == sockfd){
				acc = accept(sockfd, &clientsa, &sa_size);
				if(acc < 0)
					perror("accept");
				if(pfd_last != 0){
					pfds[pfd_last].fd = acc;
					pfd_last = 0;
				}else{
					pfds[pfd_count++].fd = acc;
				}
				break;
			}
			handle_request(pf->fd, &http_conn);
			break;
		case POLLNVAL:
			printf("POLLNVAL\n");	
			if(pfd_last == 0){
				pfd_last = i;
			}
			pf->fd = -1;
			break;
		case POLLERR:
			perror("POLLERR");
			break;	
		}
	}
	}
	printf("Exiting...\n");
	close(sockfd);
	hmsg_free(request);
	hmsg_free(response);
	return 0;
}