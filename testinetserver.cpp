/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
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
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "spprocinet.hpp"
#include "spprocpdu.hpp"
#include "spprocpool.hpp"

#define MAXN    16384           /* max # bytes client can request */
#define MAXLINE         4096    /* max text line length */

class SP_ProcUnpService : public SP_ProcInetService {
public:
	SP_ProcUnpService() {}
	virtual ~SP_ProcUnpService() {}

	virtual void handle( int sockfd ) {
		int ntowrite;
		ssize_t nread;
		char line[MAXLINE], result[MAXN];

		for ( ; ; ) {
			if ( (nread = read(sockfd, line, MAXLINE)) == 0) {
				return;		/* connection closed by other end */
			}

			/* line from client specifies #bytes to write back */
			ntowrite = atol(line);
			if ((ntowrite <= 0) || (ntowrite > MAXN)) {
				syslog( LOG_WARNING, "WARN: client request for %d bytes", ntowrite);
				exit( -1 );
			}

			SP_ProcPduUtils::writen(sockfd, result, ntowrite);
		}
	}
};

class SP_ProcUnpServiceFactory : public SP_ProcInetServiceFactory {
public:
	SP_ProcUnpServiceFactory() {}
	virtual ~SP_ProcUnpServiceFactory() {}

	virtual SP_ProcInetService * create() const {
		return new SP_ProcUnpService();
	}

	virtual void workerInit( const SP_ProcInfo * procInfo ) {
		signal( SIGINT, SIG_DFL );
		printf( "pid %d start\n", (int)procInfo->getPid() );
	}

	virtual void workerEnd( const SP_ProcInfo * procInfo ) {
		printf( "pid %d, pipeFd %d, requests %d, lastActiveTime %ld\n",
				(int)procInfo->getPid(), procInfo->getPipeFd(),
				procInfo->getRequests(), procInfo->getLastActiveTime() );
	}
};

void sig_int(int signo)
{
	SP_ProcPduUtils::print_cpu_time();

	kill( 0, SIGUSR1 );
}

int main( int argc, char * argv[] )
{
#ifdef LOG_PERROR
	openlog( "testinetserver", LOG_CONS | LOG_PID | LOG_PERROR, LOG_USER );
#else
	openlog( "testinetserver", LOG_CONS | LOG_PID, LOG_USER );
#endif

	setlogmask( LOG_UPTO( LOG_INFO ) );

	printf( "This test case is similar to the <Unix Network Programming, V1, Third Ed>\n" );
	printf( "chapter 30.9 TCP Preforked Server, Descriptor Passing\n" );
	printf( "You can run the testinetclient to communicate with this server\n\n" );

	int port = 1770, procCount = 10;

	printf( "testinetserver listen on port [%d]\n", port );

	signal( SIGINT, sig_int );

	SP_ProcInetServer server( "", port, new SP_ProcUnpServiceFactory() );

	// make a fixed number proc pool
	server.setMaxProc( procCount );
	server.setMaxIdleProc( procCount );
	server.setMinIdleProc( procCount );

	server.start();

	closelog();

	return 0;
}

