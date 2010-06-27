/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <string.h>
#include <assert.h>
#include <syslog.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>

#include "spproclfsvr.hpp"

#include "spprocserver.hpp"
#include "spprocmanager.hpp"
#include "spprocpool.hpp"
#include "spprocpdu.hpp"
#include "spproclock.hpp"

class SP_ProcWorkerLFAdapter : public SP_ProcWorker {
public:
	SP_ProcWorkerLFAdapter( int listenfd, int podfd, SP_ProcInetServiceFactory * factory );
	~SP_ProcWorkerLFAdapter();

	void setMaxRequestsPerProc( int maxRequestsPerProc );

	virtual void process( SP_ProcInfo * procInfo );

	void setAcceptLock( SP_ProcLock * lock );

private:
	int mListenfd, mPodfd;
	SP_ProcInetServiceFactory * mFactory;
	SP_ProcLock * mLock;

	int mMaxRequestsPerProc;
};

SP_ProcWorkerLFAdapter :: SP_ProcWorkerLFAdapter( int listenfd, int podfd, SP_ProcInetServiceFactory * factory )
{
	mListenfd = listenfd;
	mPodfd = podfd;
	mFactory = factory;
	mLock = NULL;

	mMaxRequestsPerProc = 0;
}

SP_ProcWorkerLFAdapter :: ~SP_ProcWorkerLFAdapter()
{
	delete mFactory;
	mFactory = NULL;
}

void SP_ProcWorkerLFAdapter :: setMaxRequestsPerProc( int maxRequestsPerProc )
{
	mMaxRequestsPerProc = maxRequestsPerProc;
}

void SP_ProcWorkerLFAdapter :: setAcceptLock( SP_ProcLock * lock )
{
	mLock = lock;
}

void SP_ProcWorkerLFAdapter :: process( SP_ProcInfo * procInfo )
{
	mFactory->workerInit( procInfo );

	int flags = 0;
	assert( ( flags = fcntl( mPodfd, F_GETFL, 0 ) ) >= 0 );
	flags |= O_NONBLOCK;
	assert( fcntl( mPodfd, F_SETFL, flags ) >= 0 );

	for( ; ( 0 == mMaxRequestsPerProc )
			|| ( mMaxRequestsPerProc > 0 && procInfo->getRequests() < mMaxRequestsPerProc ); ) {

		struct sockaddr_in clientAddr;
		socklen_t clientLen = sizeof( clientAddr );

		if( NULL != mLock ) assert( 0 == mLock->lock() );

		int fd = accept( mListenfd, (struct sockaddr *)&clientAddr, &clientLen );

		if( NULL != mLock ) assert( 0 == mLock->unlock() );

		if( fd >= 0 ) {
			assert( write( procInfo->getPipeFd(), &SP_ProcInfo::CHAR_BUSY, 1 ) > 0 );

			SP_ProcInetService * service = mFactory->create();
			service->handle( fd );
			close( fd );
			delete service;

			assert( write( procInfo->getPipeFd(), &SP_ProcInfo::CHAR_IDLE, 1 ) > 0 );

			procInfo->setRequests( procInfo->getRequests() + 1 );
		} else {
			syslog( LOG_WARNING, "WARN: accept fail, errno %d, %s", errno, strerror( errno ) );

			if( errno == EWOULDBLOCK || errno == ECONNABORTED || errno == EPROTO || errno == EINTR ) {
				// ignore these errno
			} else {
				assert( write( procInfo->getPipeFd(), &SP_ProcInfo::CHAR_EXIT, 1 ) > 0 );
				break;
			}
		}

		char pod = 0;
		if( read( mPodfd, &pod, 1 ) > 0 ) {
			assert( write( procInfo->getPipeFd(), &SP_ProcInfo::CHAR_EXIT, 1 ) > 0 );
			break;
		}
	}

	procInfo->setLastActiveTime( time( NULL ) );

	mFactory->workerEnd( procInfo );
}

//-------------------------------------------------------------------

class SP_ProcWorkerFactoryLFAdapter : public SP_ProcWorkerFactory {
public:
	SP_ProcWorkerFactoryLFAdapter( int listenfd, int podfd, SP_ProcInetServiceFactory * factory );
	virtual ~SP_ProcWorkerFactoryLFAdapter();

	void setMaxRequestsPerProc( int maxRequestsPerProc );

	void setAcceptLock( SP_ProcLock * lock );

	virtual SP_ProcWorker * create() const;

private:
	int mListenfd, mPodfd;
	SP_ProcInetServiceFactory * mFactory;
	SP_ProcLock * mLock;

	int mMaxRequestsPerProc;
};

SP_ProcWorkerFactoryLFAdapter :: SP_ProcWorkerFactoryLFAdapter(
		int listenfd, int podfd, SP_ProcInetServiceFactory * factory )
{
	mListenfd = listenfd;
	mPodfd = podfd;
	mFactory = factory;
	mLock = NULL;

	mMaxRequestsPerProc = 0;
}

SP_ProcWorkerFactoryLFAdapter :: ~SP_ProcWorkerFactoryLFAdapter()
{
	delete mFactory;
	mFactory = NULL;
}

void SP_ProcWorkerFactoryLFAdapter :: setMaxRequestsPerProc( int maxRequestsPerProc )
{
	mMaxRequestsPerProc = maxRequestsPerProc;
}

void SP_ProcWorkerFactoryLFAdapter :: setAcceptLock( SP_ProcLock * lock )
{
	mLock = lock;
}

SP_ProcWorker * SP_ProcWorkerFactoryLFAdapter :: create() const
{
	SP_ProcWorkerLFAdapter * worker = new SP_ProcWorkerLFAdapter( mListenfd, mPodfd, mFactory );
	worker->setMaxRequestsPerProc( mMaxRequestsPerProc );
	worker->setAcceptLock( mLock );

	return worker;
}

//-------------------------------------------------------------------

SP_ProcLFServer :: SP_ProcLFServer( const char * bindIP, int port,
		SP_ProcInetServiceFactory * factory )
	: SP_ProcBaseServer( bindIP, port, factory )
{
	mLock = NULL;
}

SP_ProcLFServer :: ~SP_ProcLFServer()
{
}

void SP_ProcLFServer :: setAcceptLock( SP_ProcLock * lock )
{
	mLock = lock;
}

int SP_ProcLFServer :: start()
{
	/* Don't die with SIGPIPE on remote read shutdown. That's dumb. */
	signal( SIGPIPE, SIG_IGN );

	// filedes[0] is for reading, filedes[1] is for writing
	int podfds[ 2 ] = { 0 };
	assert( 0 == pipe( podfds ) );

	int listenfd = -1;
	assert( 0 == SP_ProcPduUtils::tcp_listen( mBindIP, mPort, &listenfd ) );

	SP_ProcWorkerFactoryLFAdapter * factory =
			new SP_ProcWorkerFactoryLFAdapter( listenfd, podfds[0], mFactory );
	factory->setMaxRequestsPerProc( mMaxRequestsPerProc );
	factory->setAcceptLock( mLock );

	SP_ProcManager procManager( factory );
	procManager.start();
	SP_ProcPool * procPool = procManager.getProcPool();

	close( podfds[0] );
	close( listenfd );

	SP_ProcInfoList procList;

	for( int i = 0; i < mArgs->mMinIdleProc; i++ ) {
		SP_ProcInfo * info = procPool->get();
		if( NULL != info ) {
			procList.append( info );
		} else {
			syslog( LOG_WARNING, "WARN: Create proc fail, only %d idle proc",
					procList.getCount() );
			break;
		}
	}

	int idleCount = procList.getCount();

	mIsStop = 0;

	for( ; 0 == mIsStop ; ) {
		fd_set rset;
		FD_ZERO( &rset );

		int maxfd = 0;

		for( int i = 0; i < procList.getCount(); i++ ) {
			const SP_ProcInfo * iter = procList.getItem( i );
			FD_SET( iter->getPipeFd(), &rset );
			maxfd = maxfd > iter->getPipeFd() ? maxfd : iter->getPipeFd();
		}

		int nsel = select( maxfd + 1, &rset, NULL, NULL, NULL );

		/* find out the child is busy/idle/exit */
		for( int i = procList.getCount() - 1; i >= 0; i-- ) {
			SP_ProcInfo * iter = procList.getItem( i );

			if( FD_ISSET( iter->getPipeFd(), &rset ) ) {
				int isProcExit = 0;

				char buff[ 128 ] = { 0 };
				int len = recv( iter->getPipeFd(), buff, sizeof( buff ), MSG_DONTWAIT );
				if( len > 0 ) {
					iter->setRequests( iter->getRequests() + len );
					if( SP_ProcInfo::CHAR_IDLE == buff[ len - 1 ] ) {
						if( ! iter->isIdle() ) idleCount++;
						iter->setIdle( 1 );
					} else if( SP_ProcInfo::CHAR_BUSY == buff[ len - 1 ] ) {
						if( iter->isIdle() ) idleCount--;
						iter->setIdle( 0 );
					} else {
						isProcExit = 1;
					}
				} else if( 0 == len ) {
					isProcExit = 1;
				}

				if( isProcExit ) {
					syslog( LOG_INFO, "INFO: proc #%u exit", iter->getPid() );
					if( iter->isIdle() ) idleCount--;
					iter = procList.takeItem( i );
					procPool->erase( iter );
				}
				if (--nsel == 0) break;	/* all done with select() results */
			}
		}

		if( idleCount > mArgs->mMaxIdleProc ) {
			assert( write( podfds[1], &SP_ProcInfo::CHAR_EXIT, 1 ) > 0 );
			syslog( LOG_INFO, "INFO: idle.count %d, max.idle %d, send a pod",
					idleCount, mArgs->mMaxIdleProc );

			for( int i = 0; i < procList.getCount(); i++ ) {
				SP_ProcInfo * iter = procList.getItem( i );
				if( iter->isIdle() ) {
					iter->setIdle( 0 );
					idleCount--;
					break;
				}
			}
		}

		if( idleCount < mArgs->mMinIdleProc && procList.getCount() < mArgs->mMaxProc ) {
			SP_ProcInfo * info = procPool->get();
			if( NULL != info ) {
				idleCount++;
				procList.append( info );
			} else {
				syslog( LOG_WARNING, "WARN: Create proc fail, only %d idle proc", idleCount );
			}
		}
	}

	return 0;
}

