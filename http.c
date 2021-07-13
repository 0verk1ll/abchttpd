// SPDX-FileCopyrightText: 2021 2017 rdci
//
// SPDX-License-Identifier: mit

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

#include "hash.h"
#include "subr.h"

/* this is a simple http server */

#define USER_AGENT	"abchttpd 0.0.0"
#define PORT 		3003
#define HDEFAULT 	"index.html"
#define HOST		"ia.jumpingcrab.com:3003"

#define FIELDS		32
#define FIELD_LEN	1024 
#define BLOCK_SZ 	64	
#define PATH_MAX 	4096
#define CLIENTS_MAX	32

#define PERS_TIME	15

static int done = 0;

struct http_msg{
	char * status;
	struct bucket * fields;
	char * message_body;
	int clen;
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
	h->clen = 0;
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

int send_response(int fd, struct http_msg * htmsg)
{
	assert(fd && htmsg);
	size_t slen = 0;
	char * status = htmsg->status;
	int fb_len = 100;
	char * field_buffer = calloc(1,fb_len);
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

	sprintf(field_buffer, "Content-Length: %i\n", htmsg->clen); 
	write(fd, field_buffer, strlen(field_buffer));
	
	write(fd, sep, strlen(sep));
	int w = 0;
	w = write(fd, htmsg->message_body, htmsg->clen);
	printf("wrote %i\n", w);
	free(field_buffer);
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
	response->clen = strlen(response->message_body);
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

unsigned long loadresrc(char * path, char ** buf)
{
	unsigned long file_size = 0;
	FILE * file = fopen(path, "r");
	if(!file) return -1;
	
	fseek(file, 0, SEEK_END);
	file_size = ftell(file);
	
	rewind(file);
	
	*buf = calloc(file_size, 1);
	if(!(*buf))
		return -1;		
	if(fread(*buf, 1, file_size, file) != file_size){
		printf("file not completely read\n");	
		return -1;
	}
	return file_size;	
}

int handle_get(int fd, char * resource, struct connection * conn)
{
	assert(resource != NULL && conn != NULL);
	char * p = NULL, * pathbuf = NULL;
	struct http_msg ** htpr = &conn->response;

	pathbuf = alloca(PATH_MAX);
	char * pb = make_path(resource, pathbuf);
	int clen = loadresrc(pb, &p);
	if(p){
		printf("200 %i\n", clen);
		(*htpr)->message_body = p;
		(*htpr)->clen = clen;
		send_code_200(fd, conn->response);
		free(p);
	}else{
		printf("404\n");
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
	printf("action: %s\n", action);

	assert(conn->request->message_body != NULL);
	int mblen = strlen(conn->request->message_body);
	if(mblen <= 0){
		exit(EXIT_FAILURE);
	}
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
		char * buffer = alloca(buffer_size+1);
		if((r = read(pfds[0], buffer, buffer_size)) < 0){
			perror("forked_parent_read");
			buffer = "forked_parent_read";
		}
		buffer[r] = '\0';
		wait(NULL);
		h->message_body = buffer;
		h->clen = strlen(buffer);
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
