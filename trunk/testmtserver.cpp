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
#include <assert.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "spprocmtsvr.hpp"
#include "spprocpdu.hpp"
#include "spprocpool.hpp"
#include "spproclock.hpp"

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
		printf( "pid %d exit, pipeFd %d, requests %d, lastActiveTime %ld\n",
				(int)procInfo->getPid(), procInfo->getPipeFd(),
				procInfo->getRequests(), procInfo->getLastActiveTime() );
	}
};

void sig_int(int signo)
{
	printf( "\nproc: %d\n", (int)getpid() );

	SP_ProcPduUtils::print_cpu_time();

	kill( 0, SIGUSR1 );
}

int main( int argc, char * argv[] )
{
	int port = 1770, procCount = 10;
	char lockType = '0';

	extern char *optarg ;
	int c ;

	while( ( c = getopt ( argc, argv, "p:c:l:v" )) != EOF ) {
		switch ( c ) {
			case 'p' :
				port = atoi( optarg );
				break;
			case 'c':
				procCount = atoi( optarg );
				break;
			case 'l':
				lockType = *optarg;
				break;
			case '?' :
			case 'v' :
				printf( "Usage: %s [-p <port>] [-c <proc count>] [-l <f|t>]\n", argv[0] );
				exit( 0 );
		}
	}

#ifdef LOG_PERROR
	openlog( "testmtserver", LOG_CONS | LOG_PID | LOG_PERROR, LOG_USER );
#else
	openlog( "testmtserver", LOG_CONS | LOG_PID, LOG_USER );
#endif

	setlogmask( LOG_UPTO( LOG_INFO ) );

	printf( "This test case is similar to apache's worker mpm\n" );
	printf( "You can run the testinetclient to communicate with this server\n\n" );

	printf( "testproclfsvr listen on port [%d]\n", port );

	signal( SIGINT, sig_int );

	SP_ProcMTServer server( "", port, new SP_ProcUnpServiceFactory() );

	// make a fixed number proc pool
	SP_ProcArgs_t args = { procCount, procCount, procCount };
	server.setArgs( &args );
	server.setThreadsPerProc( 10 );
	server.setMaxRequestsPerProc( 1000 );

	SP_ProcLock * lock = NULL;
	if( 'f' == lockType || 'F' == lockType ) {
		lock = new SP_ProcFileLock();
		assert( 0 == ((SP_ProcFileLock*)lock)->init( "/tmp/testmtserver.lck" ) );
	} else if( 't' == lockType || 'T' == lockType ) {
		lock = new SP_ProcThreadLock();
	} else {
		// no locking
	}

	server.setAcceptLock( lock );

	server.start();

	closelog();

	return 0;
}

