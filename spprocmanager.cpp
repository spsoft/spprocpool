/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "spprocmanager.hpp"
#include "spprocpool.hpp"
#include "spprocpdu.hpp"

SP_ProcWorker :: ~SP_ProcWorker()
{
}

//-------------------------------------------------------------------

SP_ProcWorkerFactory :: ~SP_ProcWorkerFactory()
{
}

//-------------------------------------------------------------------

SP_ProcManager :: SP_ProcManager( SP_ProcWorkerFactory * factory )
{
	mFactory = factory;

	mPool = NULL;
}

SP_ProcManager :: ~SP_ProcManager()
{
	delete mFactory;
	mFactory = NULL;

	if( NULL != mPool ) delete mPool;
	mPool = NULL;
}

void SP_ProcManager :: sigchild( int signo )
{
	pid_t pid;
	int stat;

	while( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 ) {
	}
}

void SP_ProcManager :: start()
{
	int pipeFd[ 2 ] = { -1, -1 };

	if( 0 == socketpair( AF_UNIX, SOCK_STREAM, 0, pipeFd ) ) {
		pid_t pid = fork();

		if( pid > 0 ) {
			// parent, app process
			close( pipeFd[1] );

			mPool = new SP_ProcPool( pipeFd[0] );

		} else if( 0 == pid ) {
			// child, process manager
			signal( SIGCHLD, sigchild );

			close( pipeFd[0] );

			for( ; ; ) {
				int fd = SP_ProcPduUtils::recv_fd( pipeFd[1] );
				if( fd >= 0 ) {
					SP_ProcPdu_t pdu;
					memset( &pdu, 0, sizeof( pdu ) );
					pdu.mMagicNum = SP_ProcPdu_t::MAGIC_NUM;
					pdu.mDestPid = getppid();

					pid_t workerPid = fork();
					if( 0 == workerPid ) {
						// worker, working

						SP_ProcInfo * info = new SP_ProcInfo( fd );
						info->setPid( getpid() );

						SP_ProcWorker * worker = mFactory->create();
						worker->process( info );

						delete info;
						delete worker;

						exit( 0 );
					} else {
						// parent, notify app
						if( workerPid > 0 ) {
							pdu.mSrcPid = workerPid;
						} else {
							pdu.mSrcPid = -1 * errno;
						}

						if( SP_ProcPduUtils::send_pdu( pipeFd[1], &pdu, NULL ) < 0 ) {
							kill( 0, SIGUSR1 );
							break;
						}

						close( fd );
					}
				} else {
					if( 0 == errno ) {
						syslog( LOG_INFO, "INFO: proc manager exit" );
					} else {
						syslog( LOG_WARNING, "WARN: recv fd fail, errno %d, %s", errno, strerror( errno ) );
					}
					kill( 0, SIGUSR1 );
					break;
				}
			}
		} else {
			close( pipeFd[0] );
			close( pipeFd[1] );

			perror( "fork fail" );
			exit( -1 );
		}
	}
}

SP_ProcPool * SP_ProcManager :: getProcPool()
{
	return mPool;
}

