/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <syslog.h>
#include <errno.h>
#include <signal.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "spprocinet.hpp"

#include "spprocpdu.hpp"
#include "spprocpool.hpp"
#include "spprocmanager.hpp"

SP_ProcInetService :: ~SP_ProcInetService()
{
}

//-------------------------------------------------------------------

SP_ProcInetServiceFactory :: ~SP_ProcInetServiceFactory()
{
}

void SP_ProcInetServiceFactory :: workerInit( const SP_ProcInfo * procInfo )
{
}

void SP_ProcInetServiceFactory :: workerEnd( const SP_ProcInfo * procInfo )
{
}

//-------------------------------------------------------------------

class SP_ProcWorkerInetAdapter : public SP_ProcWorker {
public:
	SP_ProcWorkerInetAdapter( SP_ProcInetServiceFactory * factory );
	~SP_ProcWorkerInetAdapter();

	virtual void process( SP_ProcInfo * procInfo );

private:
	SP_ProcInetServiceFactory * mFactory;
};

SP_ProcWorkerInetAdapter :: SP_ProcWorkerInetAdapter( SP_ProcInetServiceFactory * factory )
{
	mFactory = factory;
}

SP_ProcWorkerInetAdapter :: ~SP_ProcWorkerInetAdapter()
{
	delete mFactory;
	mFactory = NULL;
}

void SP_ProcWorkerInetAdapter :: process( SP_ProcInfo * procInfo )
{
	mFactory->workerInit( procInfo );

	for( ; ; ) {
		int fd = SP_ProcPduUtils::recv_fd( procInfo->getPipeFd() );
		if( fd >= 0 ) {
			procInfo->setRequests( procInfo->getRequests() + 1 );
			SP_ProcInetService * service = mFactory->create();

			service->handle( fd );
			close( fd );

			delete service;

			SP_ProcPdu_t replyPdu;
			memset( &replyPdu, 0, sizeof( SP_ProcPdu_t ) );
			replyPdu.mMagicNum = SP_ProcPdu_t::MAGIC_NUM;
			replyPdu.mSrcPid = getpid();

			if( SP_ProcPduUtils::send_pdu( procInfo->getPipeFd(), &replyPdu, NULL ) < 0 ) {
				break;
			}
		} else {
			syslog( LOG_WARNING, "WARN: recv_fd fail, errno %d, %s", errno, strerror( errno ) );
			break;
		}
	}

	procInfo->setLastActiveTime( time( NULL ) );
	mFactory->workerEnd( procInfo );
}

//-------------------------------------------------------------------

class SP_ProcWorkerFactoryInetAdapter : public SP_ProcWorkerFactory {
public:
	SP_ProcWorkerFactoryInetAdapter( SP_ProcInetServiceFactory * factory );
	virtual ~SP_ProcWorkerFactoryInetAdapter();

	virtual SP_ProcWorker * create() const;

private:
	SP_ProcInetServiceFactory * mFactory;
};


SP_ProcWorkerFactoryInetAdapter :: SP_ProcWorkerFactoryInetAdapter(
		SP_ProcInetServiceFactory * factory )
{
	mFactory = factory;
}

SP_ProcWorkerFactoryInetAdapter :: ~SP_ProcWorkerFactoryInetAdapter()
{
	delete mFactory;
	mFactory = NULL;
}

SP_ProcWorker * SP_ProcWorkerFactoryInetAdapter :: create() const
{
	return new SP_ProcWorkerInetAdapter( mFactory );
}

//-------------------------------------------------------------------

SP_ProcInetServer :: SP_ProcInetServer( const char * bindIP, int port,
		SP_ProcInetServiceFactory * factory )
{
	strncpy( mBindIP, bindIP, sizeof( mBindIP ) );
	mBindIP[ sizeof( mBindIP ) - 1 ] = '\0';

	mPort = port;

	mFactory = factory;

	mArgs = (SP_ProcArgs_t*)malloc( sizeof( SP_ProcArgs_t ) );
	mArgs->mMaxProc = 64;
	mArgs->mMaxIdleProc = 5;
	mArgs->mMinIdleProc = 1;
	mArgs->mMaxRequestsPerProc = 0;

	mIsStop = 1;
}

SP_ProcInetServer :: ~SP_ProcInetServer()
{
	free( mArgs );
	mArgs = NULL;
}

void SP_ProcInetServer :: setArgs( const SP_ProcArgs_t * args )
{
	* mArgs = * args;

	if( mArgs->mMinIdleProc <= 0 ) mArgs->mMinIdleProc = 1;
	if( mArgs->mMaxIdleProc < mArgs->mMinIdleProc ) mArgs->mMaxIdleProc = mArgs->mMinIdleProc;
	if( mArgs->mMaxProc <= 0 ) mArgs->mMaxProc = mArgs->mMaxIdleProc;
}

void SP_ProcInetServer :: getArgs( SP_ProcArgs_t * args ) const
{
	* args = * mArgs;
}

void SP_ProcInetServer :: shutdown()
{
	mIsStop = 1;
}

int SP_ProcInetServer :: isStop()
{
	return mIsStop;
}

int SP_ProcInetServer :: start()
{
	/* Don't die with SIGPIPE on remote read shutdown. That's dumb. */
	signal( SIGPIPE, SIG_IGN );

	int listenfd = -1;
	assert( 0 == SP_ProcPduUtils::tcp_listen( mBindIP, mPort, &listenfd ) );

	SP_ProcManager procManager( new SP_ProcWorkerFactoryInetAdapter( mFactory ) );
	procManager.start();
	SP_ProcPool * procPool = procManager.getProcPool();

	procPool->setMaxRequestsPerProc( mArgs->mMaxRequestsPerProc );
	procPool->setMaxIdleProc( mArgs->mMaxIdleProc );
	procPool->ensureIdleProc( mArgs->mMinIdleProc );

	SP_ProcInfoList busyList;

	mIsStop = 0;

	for ( ; 0 == mIsStop; ) {
		fd_set rset;
		FD_ZERO( &rset );

		FD_SET( listenfd, &rset );
		int maxfd = listenfd;

		for( int i = 0; i < busyList.getCount(); i++ ) {
			const SP_ProcInfo * iter = busyList.getItem( i );
			FD_SET( iter->getPipeFd(), &rset );
			maxfd = maxfd > iter->getPipeFd() ? maxfd : iter->getPipeFd();
		}

		if( busyList.getCount() >= mArgs->mMaxProc ) FD_CLR( listenfd, &rset );

		int nsel = 0;

		for( ; 0 == mIsStop && nsel <= 0; ) {
			nsel = select( maxfd + 1, &rset, NULL, NULL, NULL );
		}

		/* check for new connections */
		if( FD_ISSET( listenfd, &rset ) && busyList.getCount() < mArgs->mMaxProc ) {
			SP_ProcInfo * info = procPool->get();

			if( NULL != info ) {
				struct sockaddr_in clientAddr;
				socklen_t clientLen = sizeof( clientAddr );
				int clientFd = accept( listenfd, (struct sockaddr *)&clientAddr, &clientLen );

				if( clientFd >= 0 ) {
					if( 0 == SP_ProcPduUtils::send_fd( info->getPipeFd(), clientFd ) ) {
						busyList.append( info );
					} else {
						procPool->erase( info );
					}
					close( clientFd );
				} else {
					procPool->save( info );
				}
			}

			if (--nsel == 0) continue;	/* all done with select() results */
		}

		/* find any newly-available children */
		for( int i = busyList.getCount() - 1; i >= 0; i-- ) {
			const SP_ProcInfo * iter = busyList.getItem( i );
			if( FD_ISSET( iter->getPipeFd(), &rset ) ) {
				SP_ProcInfo * info = busyList.takeItem( i );

				SP_ProcPdu_t pdu;
				if( SP_ProcPduUtils::read_pdu( iter->getPipeFd(), &pdu, NULL ) > 0 ) {
					assert( info->getPid() == pdu.mSrcPid );
					procPool->save( info );
				} else {
					procPool->erase( info );
				}

				if (--nsel == 0) break;	/* all done with select() results */
			}
		}

		int idleCount = procPool->getIdleCount();
		int totalCount = idleCount + busyList.getCount();
		if( ( idleCount < mArgs->mMinIdleProc ) && ( totalCount < mArgs->mMaxProc ) ) {
			procPool->ensureIdleProc( procPool->getIdleCount() + 1 );
		}
	}

	close( listenfd );

	return 0;
}

