/*
 * It's adapted from UNP ( by W. Richard Stevens ).
 */

#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "spprocpdu.hpp"


#define	MAXN	16384		/* max # bytes to request from server */
#define MAXLINE         4096    /* max text line length */

int tcp_connect( const char *host, int port )
{
	int sockfd;
	const int on = 1;

	struct sockaddr_in inAddr;
	memset( &inAddr, 0, sizeof( inAddr ) );
	inAddr.sin_family = AF_INET;

	inAddr.sin_addr.s_addr = inet_addr( host );
	inAddr.sin_port = htons( port );

	sockfd = socket( AF_INET, SOCK_STREAM, IPPROTO_IP );
	if (sockfd < 0) return -1;

	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	if( 0 != connect( sockfd, (struct sockaddr*)&inAddr, sizeof( inAddr ) ) ) {
		close( sockfd );
		return -1;
	}

	return(sockfd);
}

int main(int argc, char **argv)
{
	int		i, j, fd, nchildren, nloops, nbytes;
	pid_t	pid;
	ssize_t	n;
	char	request[MAXLINE], reply[MAXN];

	if (argc != 6) {
		printf("usage: client <hostname or IPaddr> <port> <#children> "
				 "<#loops/child> <#bytes/request>\n");
		exit( -1 );
	}

	nchildren = atoi(argv[3]);
	nloops = atoi(argv[4]);
	nbytes = atoi(argv[5]);
	snprintf(request, sizeof(request), "%d\n", nbytes); /* newline at end */

	for (i = 0; i < nchildren; i++) {
		if ( (pid = fork()) == 0) {		/* child */
			SP_ProcClock clock;

			int gt10ms = 0, lt10ms = 0, connFail = 0;

			for (j = 0; j < nloops; j++) {
				fd = tcp_connect(argv[1], atoi( argv[2]) );

				int connectTime = clock.getInterval();

				int socketTime = 0, closeTime = 0;

				if( fd >= 0 ) {
					write(fd, request, strlen(request));

					if ( (n = SP_ProcPduUtils::readn(fd, reply, nbytes)) != nbytes) {
						printf("server returned %d bytes\n", (int)n);
						exit( -1 );
					}

					socketTime = clock.getInterval();

					close(fd);		/* TIME_WAIT on client, not server */

					closeTime = clock.getInterval();
				} else {
					connFail++;
				}

				if( ( connectTime + socketTime + closeTime ) > 10 ) {
					gt10ms++;
					//printf( "child %d, loop %d, connect %d, socket %d, close %d\n",
						//i, j, connectTime, socketTime, closeTime );
				} else {
					lt10ms++;
				}
			}
			printf("child %d done, time %ld, %d ( > 10ms ), %d ( <= 10 ), conn.fail %d\n",
				i, clock.getAge(), gt10ms, lt10ms, connFail );
			exit(0);
		}
		/* parent loops around to fork() again */
	}

	while (wait(NULL) > 0)	/* now parent waits for all children */
		;
	if (errno != ECHILD)
		printf("wait error");

	exit(0);
}

