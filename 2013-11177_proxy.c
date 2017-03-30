/*
 * proxy.c - CS:APP Web proxy
 *
 * Student ID: 2013-11177 
 *         Name: Dajung Je
 * 
 * This implementation makes a thread for each client(connfd).
 * In that thread, a while loop runs to get client's input line
 * and for each input line, open clientfd to connect to server
 * and close that connection before moving on to the next iteration.
 * 
 * Semaphores are used to make certain functions or work be thread-safe.
 * Mainly, open_clientfd_ts uses sem_t sema, 
 * logging part gets protected by sem_t log_sema,
 * and printf functions get protected by sem_t prt_sema.
 *
 * rio function wrappers were defined and used to handle error
 * but do not make thread to terminate.  
 */ 

#include "csapp.h"

/* The name of the proxy's log file */
#define PROXY_LOG "proxy.log"

/* Undefine this if you don't want debugging output */
#define DEBUG

/*
 * Functions to define
 */
void *process_request(void* vargp);
int open_clientfd_ts(char *hostname, int port, sem_t *mutexp);
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
void Rio_writen_w(int fd, void *usrbuf, size_t n);


sem_t sema;
sem_t log_sema;
sem_t prt_sema;
int proxy_port;

void *process_request(void* vargp){

		/* as echo server */
		int connfd = *((int *)vargp);
		Free(vargp);
		pthread_t tid = pthread_self();
		Pthread_detach(tid);
		char prefix[40];

		P(&prt_sema);
		printf("Served by thread %u\n", (unsigned int) tid);
		sprintf(prefix, "Thread %u ", (unsigned int) tid);
		V(&prt_sema);

		/* echo */
		size_t n;
		char buf[MAXLINE];
		rio_t rio;
		int bytecnt = 0;

		Rio_readinitb(&rio, connfd);
		while((n = Rio_readlineb_w(&rio, buf, MAXLINE))!= 0){
			
			bytecnt +=n;
			P(&prt_sema);
			printf("%s received %d bytes (%d total)\n", prefix, (int)n, bytecnt);
			fflush(stdout);
			V(&prt_sema);

			/* parsing */
			char *host, *content; int port;
			int clientfd;

			host = strtok(buf, " \n\0");
			if(host==NULL){
				perror("hostname error");
				Rio_writen_w(connfd, "hostname error\n", 15);
				continue;
			}
			char *temp = strtok(NULL, " \n\0");
			if(temp == NULL){
				perror("port number error");
				Rio_writen_w(connfd, "port num error\n", 15);
				continue;
			} else {
				port = atoi(temp);
			}
			content = strtok(NULL, "\0");
			if(content == NULL){
				perror("content error");
				Rio_writen_w(connfd, "content error\n", 14);
			}

			
			P(&prt_sema);
			printf("Hostname : %s, port : %d, string : %s", host, port, content);
			V(&prt_sema);
		
			/* check if port number is same as proxy port number */ 
			if(port == proxy_port){
				toupper(content);
				Rio_writen_w(connfd, content, strlen(content));	
			
			/* if not, that is if normal */
			} else {

				/* get clientfd */ 
				clientfd = open_clientfd_ts(host, port, &sema);
				if(clientfd < 0){
					perror("clientfd error");
					Rio_writen_w(connfd, "clientfd error\n",15);
					continue;
				}

				/* write to the server, read from server, write to client */
				Rio_writen_w(clientfd, content, strlen(content));
				Rio_readinitb(&rio, clientfd);
				Rio_readlineb_w(&rio, content, MAXLINE);
				Rio_writen_w(connfd, content, strlen(content));
			}

			/* write log */
			
			char logbuf[MAXLINE];
			char log[MAXLINE];
			struct sockaddr_in clientaddr;
			int client_len = sizeof(clientaddr);


			P(&log_sema);
		
			int logfd = Open(PROXY_LOG, O_CREAT|O_WRONLY|O_APPEND, 0666);
			time_t time_log;
			time(&time_log);
			struct tm *t_temp = localtime(&time_log);
	
			if(getpeername(connfd, (SA *) &clientaddr, &client_len) != 0){
				perror("getpeername error");
				Rio_writen_w(connfd, "getpeername erorr\n", 18); 
				continue;
			}

			strftime(logbuf, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", t_temp);
			sprintf(log, "%s: %s %d %d %s", logbuf, inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), strlen(content), content);
			Rio_writen_w(logfd, log, strlen(log));
			
			V(&log_sema);
			Close(logfd);

			/* prepare for the next iteration */
			Close(clientfd);	
			Rio_readinitb(&rio, connfd);
		}

		Close(connfd);
		return NULL;
}

int open_clientfd_ts(char* hostname, int port, sem_t *mutexp){

	P(mutexp);
	/* content from Open_clientfd of csapp.c */
	int clientfd;
	struct hostent *hp;
	struct sockaddr_in serveraddr;

	if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		V(mutexp);
		return -1; /* check errno for cause of error */
	}

	/* Fill in the server's IP address and port */
	if ((hp = gethostbyname(hostname)) == NULL){
		V(mutexp);
		return -2; /* check h_errno for cause of error */
	}

	bzero((char *) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	bcopy((char *) hp->h_addr_list[0],
			(char *) &serveraddr.sin_addr.s_addr, hp->h_length);
	serveraddr.sin_port = htons(port);

	/* Establish a connection with the server */
	if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0){
		V(mutexp);
		return -1;
	}

	V(mutexp);
	return clientfd;
}

ssize_t Rio_readn_w(int fd, void* ptr, size_t nbytes){
	
	ssize_t n;

	if ((n = rio_readn(fd, ptr, nbytes))< 0)
		perror("Rio_readn_w error");

	return n;


}

ssize_t Rio_readlineb_w(rio_t* rp, void* usrbuf, size_t maxlen){
	
	ssize_t rc;

	if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0)
		perror("Rio_readnb error");

	return rc;
}

void Rio_writen_w(int fd, void* usrbuf, size_t n){

	if (rio_writen(fd, usrbuf, n) != n)
		perror("Rio_writen error");
}


/*
 * main - Main routine for the proxy program
 */

int main(int argc, char **argv)
{
    /* Check arguments */
    if (argc != 2) {
				P(&prt_sema);
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
				V(&prt_sema);
        exit(0);
    }

		/* as echo server */
	
		Signal(SIGPIPE, SIG_IGN);
		Sem_init(&sema, 0, 1);
		Sem_init(&log_sema, 0, 1);
		Sem_init(&prt_sema, 0, 1);

		proxy_port = atoi(argv[1]);
		struct sockaddr_in clientaddr;
		int clientlen = sizeof(clientaddr);
		pthread_t tid;
		int listenfd = Open_listenfd(proxy_port);
		
		while(1){
			int *connfd = Malloc(sizeof(int));
			*connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
			/* determine the domain name and IP address of the client */
			struct hostent *hp = Gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
					sizeof(clientaddr.sin_addr.s_addr), AF_INET);
			char *haddrp = inet_ntoa(clientaddr.sin_addr);
			int client_port = ntohs(clientaddr.sin_port);
			P(&prt_sema);
			printf("Server conneted to %s (%s), port %d\n", hp->h_name,
					haddrp, client_port);
			V(&prt_sema);
			Pthread_create(&tid, NULL, process_request, connfd);
		}

    exit(0);
}
