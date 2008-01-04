/*
 * Copyright 2007-2008 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <stdio.h>

#include <sys/socket.h>
#include <netinet/in.h>

#include "spprocmtsvr.hpp"
#include "spprocinet.hpp"
#include "spprocmanager.hpp"
#include "spprocpool.hpp"
#include "spproclock.hpp"
#include "spprocpdu.hpp"
#include "spprocthread.hpp"

class SP_ProcWorkerMTAdapter : public SP_ProcWorker {
public:
	SP_ProcWorkerMTAdapter( int listenfd, int podfd, SP_ProcInetServiceFactory * factory );
	~SP_ProcWorkerMTAdapter();

	void setMaxRequestsPerProc( int maxRequestsPerProc );

	void setThreadsPerProc( int threadsPerProc );

	virtual void process( SP_ProcInfo * procInfo );

	void setAcceptLock( SP_ProcLock * lock );

private:
	int mListenfd, mPodfd;
	SP_ProcInetServiceFactory * mFactory;
	SP_ProcLock * mLock;

	int mIsStop;
	int mMaxRequestsPerProc, mThreadsPerProc;

	typedef struct tagWorkerArgs {
		SP_ProcInetServiceFactory * mFactory;
		SP_ProcThreadPool * mThreadPool;
		int mPipeFd;
		int mSockFd;
	} WorkerArgs_t;

	static void workerFunc( void * args );
	static void reportFunc( void * args );
};

SP_ProcWorkerMTAdapter :: SP_ProcWorkerMTAdapter( int listenfd, int podfd, SP_ProcInetServiceFactory * factory )
{
	mListenfd = listenfd;
	mPodfd = podfd;
	mFactory = factory;
	mLock = NULL;

	mIsStop = 0;
	mMaxRequestsPerProc = 0;
	mThreadsPerProc = 10;
}

SP_ProcWorkerMTAdapter :: ~SP_ProcWorkerMTAdapter()
{
	delete mFactory;
	mFactory = NULL;
}

void SP_ProcWorkerMTAdapter :: setMaxRequestsPerProc( int maxRequestsPerProc )
{
	mMaxRequestsPerProc = maxRequestsPerProc;
}

void SP_ProcWorkerMTAdapter :: setThreadsPerProc( int threadsPerProc )
{
	mThreadsPerProc = threadsPerProc;
}

void SP_ProcWorkerMTAdapter :: setAcceptLock( SP_ProcLock * lock )
{
	mLock = lock;
}

void SP_ProcWorkerMTAdapter :: reportFunc( void * args )
{
	SP_ProcInfo * procInfo = ( SP_ProcInfo * )args;

	int ret = write( procInfo->getPipeFd(), &SP_ProcInfo::CHAR_IDLE, 1 );
	if( ret <= 0 ) {
		syslog( LOG_WARNING, "WARN: write = %d, errno %d, %s",
				ret, errno, strerror( errno ) );
	}
}

void SP_ProcWorkerMTAdapter :: workerFunc( void * args )
{
	WorkerArgs_t * workerArgs = (WorkerArgs_t*)args;

	SP_ProcInetService * service = workerArgs->mFactory->create();
	service->handle( workerArgs->mSockFd );
	close( workerArgs->mSockFd );
	delete service;

	free( workerArgs );
}

void SP_ProcWorkerMTAdapter :: process( SP_ProcInfo * procInfo )
{
	mFactory->workerInit( procInfo );

	int flags = 0;
	assert( ( flags = fcntl( mPodfd, F_GETFL, 0 ) ) >= 0 );
	flags |= O_NONBLOCK;
	assert( fcntl( mPodfd, F_SETFL, flags ) >= 0 );

	SP_ProcThreadPool * threadPool = new SP_ProcThreadPool( mThreadsPerProc );
	threadPool->setFullCallback( reportFunc, procInfo );

	for( ; ( 0 == mMaxRequestsPerProc )
			|| ( mMaxRequestsPerProc > 0 && procInfo->getRequests() < mMaxRequestsPerProc ); ) {

		threadPool->wait4idler();

		struct sockaddr_in clientAddr;
		socklen_t clientLen = sizeof( clientAddr );

		if( NULL != mLock ) assert( 0 == mLock->lock() );

		int fd = accept( mListenfd, (struct sockaddr *)&clientAddr, &clientLen );

		if( NULL != mLock ) assert( 0 == mLock->unlock() );

		if( fd >= 0 ) {
			assert( write( procInfo->getPipeFd(), &SP_ProcInfo::CHAR_BUSY, 1 ) > 0 );

			WorkerArgs_t * args = (WorkerArgs_t*)malloc( sizeof( WorkerArgs_t ) );
			args->mFactory = mFactory;
			args->mThreadPool = threadPool;
			args->mPipeFd = procInfo->getPipeFd();
			args->mSockFd = fd;

			threadPool->dispatch( workerFunc, args );

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
		if( read( mPodfd, &pod, 1 ) > 0 ) break;
	}

	delete threadPool;

	assert( write( procInfo->getPipeFd(), &SP_ProcInfo::CHAR_EXIT, 1 ) > 0 );

	procInfo->setLastActiveTime( time( NULL ) );

	mFactory->workerEnd( procInfo );
}

//-------------------------------------------------------------------

class SP_ProcWorkerFactoryMTAdapter : public SP_ProcWorkerFactory {
public:
	SP_ProcWorkerFactoryMTAdapter( int listenfd, int podfd, SP_ProcInetServiceFactory * factory );
	virtual ~SP_ProcWorkerFactoryMTAdapter();

	void setMaxRequestsPerProc( int maxRequestsPerProc );

	void setThreadsPerProc( int threadsPerProc );

	void setAcceptLock( SP_ProcLock * lock );

	virtual SP_ProcWorker * create() const;

private:
	int mListenfd, mPodfd;
	SP_ProcInetServiceFactory * mFactory;
	SP_ProcLock * mLock;

	int mMaxRequestsPerProc, mThreadsPerProc;
};

SP_ProcWorkerFactoryMTAdapter :: SP_ProcWorkerFactoryMTAdapter(
		int listenfd, int podfd, SP_ProcInetServiceFactory * factory )
{
	mListenfd = listenfd;
	mPodfd = podfd;
	mFactory = factory;
	mLock = NULL;

	mMaxRequestsPerProc = 0;
	mThreadsPerProc = 10;
}

SP_ProcWorkerFactoryMTAdapter :: ~SP_ProcWorkerFactoryMTAdapter()
{
	delete mFactory;
	mFactory = NULL;
}

void SP_ProcWorkerFactoryMTAdapter :: setMaxRequestsPerProc( int maxRequestsPerProc )
{
	mMaxRequestsPerProc = maxRequestsPerProc;
}

void SP_ProcWorkerFactoryMTAdapter :: setThreadsPerProc( int threadsPerProc )
{
	mThreadsPerProc = threadsPerProc;
}

void SP_ProcWorkerFactoryMTAdapter :: setAcceptLock( SP_ProcLock * lock )
{
	mLock = lock;
}

SP_ProcWorker * SP_ProcWorkerFactoryMTAdapter :: create() const
{
	SP_ProcWorkerMTAdapter * worker = new SP_ProcWorkerMTAdapter( mListenfd, mPodfd, mFactory );
	worker->setMaxRequestsPerProc( mMaxRequestsPerProc );
	worker->setThreadsPerProc( mThreadsPerProc );
	worker->setAcceptLock( mLock );

	return worker;
}

//-------------------------------------------------------------------

SP_ProcMTServer :: SP_ProcMTServer( const char * bindIP, int port,
			SP_ProcInetServiceFactory * factory )
	: SP_ProcBaseServer( bindIP, port, factory )
{
	mThreadsPerProc = 10;
	mLock = NULL;
}

SP_ProcMTServer :: ~SP_ProcMTServer()
{
}

void SP_ProcMTServer :: setThreadsPerProc( int threadsPerProc )
{
	mThreadsPerProc = threadsPerProc;
	if( mThreadsPerProc <= 0 ) mThreadsPerProc = 1;
}

void SP_ProcMTServer :: setAcceptLock( SP_ProcLock * lock )
{
	mLock = lock;
}

int SP_ProcMTServer :: start()
{
	/* Don't die with SIGPIPE on remote read shutdown. That's dumb. */
	signal( SIGPIPE, SIG_IGN );

	// filedes[0] is for reading, filedes[1] is for writing
	int podfds[ 2 ] = { 0 };
	assert( 0 == pipe( podfds ) );

	int listenfd = -1;
	assert( 0 == SP_ProcPduUtils::tcp_listen( mBindIP, mPort, &listenfd ) );

	SP_ProcWorkerFactoryMTAdapter * factory =
			new SP_ProcWorkerFactoryMTAdapter( listenfd, podfds[0], mFactory );
	factory->setMaxRequestsPerProc( mMaxRequestsPerProc );
	factory->setThreadsPerProc( mThreadsPerProc );
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

	for( ; 0 == mIsStop; ) {
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

