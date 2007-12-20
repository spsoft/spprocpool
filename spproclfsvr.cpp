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

#include <sys/socket.h>
#include <netinet/in.h>

#include "spproclfsvr.hpp"

#include "spprocinet.hpp"
#include "spprocmanager.hpp"
#include "spprocpool.hpp"
#include "spprocpdu.hpp"

class SP_ProcWorkerLFAdapter : public SP_ProcWorker {
public:
	SP_ProcWorkerLFAdapter( int listenfd, SP_ProcInetServiceFactory * factory );
	~SP_ProcWorkerLFAdapter();

	void setMaxRequestsPerProc( int maxRequestsPerProc );
	void setMaxIdleTimeout( int maxIdleTimeout );

	virtual void process( const SP_ProcInfo * procInfo );

private:
	int mListenfd;
	SP_ProcInetServiceFactory * mFactory;

	int mMaxRequestsPerProc, mMaxIdleTimeout;
};

SP_ProcWorkerLFAdapter :: SP_ProcWorkerLFAdapter( int listenfd, SP_ProcInetServiceFactory * factory )
{
	mListenfd = listenfd;
	mFactory = factory;

	mMaxRequestsPerProc = 0;
	mMaxIdleTimeout = 0;
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

void SP_ProcWorkerLFAdapter :: setMaxIdleTimeout( int maxIdleTimeout )
{
	mMaxIdleTimeout = maxIdleTimeout;
}

void SP_ProcWorkerLFAdapter :: process( const SP_ProcInfo * procInfo )
{
	for( int i = 0; ( 0 == mMaxRequestsPerProc )
			|| ( mMaxRequestsPerProc > 0 && i >= mMaxRequestsPerProc ); i++ ) {

		struct sockaddr_in clientAddr;
		socklen_t clientLen = sizeof( clientAddr );
		int fd = accept( mListenfd, (struct sockaddr *)&clientAddr, &clientLen );

		if( fd >= 0 ) {

			SP_ProcInetService * service = mFactory->create();

			service->handle( fd );
			close( fd );

			delete service;

		} else {
			syslog( LOG_WARNING, "WARN: accept fail, errno %d, %s", errno, strerror( errno ) );
		}
	}
}

//-------------------------------------------------------------------

class SP_ProcWorkerFactoryLFAdapter : public SP_ProcWorkerFactory {
public:
	SP_ProcWorkerFactoryLFAdapter( int listenfd, SP_ProcInetServiceFactory * factory );
	virtual ~SP_ProcWorkerFactoryLFAdapter();

	void setMaxRequestsPerProc( int maxRequestsPerProc );
	void setMaxIdleTimeout( int maxIdleTimeout );

	virtual SP_ProcWorker * create() const;

private:
	int mListenfd;
	SP_ProcInetServiceFactory * mFactory;

	int mMaxRequestsPerProc, mMaxIdleTimeout;
};

SP_ProcWorkerFactoryLFAdapter :: SP_ProcWorkerFactoryLFAdapter(
		int listenfd, SP_ProcInetServiceFactory * factory )
{
	mListenfd = listenfd;
	mFactory = factory;

	mMaxRequestsPerProc = 0;
	mMaxIdleTimeout = 0;
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

void SP_ProcWorkerFactoryLFAdapter :: setMaxIdleTimeout( int maxIdleTimeout )
{
	mMaxIdleTimeout = maxIdleTimeout;
}

SP_ProcWorker * SP_ProcWorkerFactoryLFAdapter :: create() const
{
	return new SP_ProcWorkerLFAdapter( mListenfd, mFactory );
}

//-------------------------------------------------------------------

SP_ProcLFServer :: SP_ProcLFServer( const char * bindIP, int port,
		SP_ProcInetServiceFactory * factory )
{
	strncpy( mBindIP, bindIP, sizeof( mBindIP ) );
	mBindIP[ sizeof( mBindIP ) - 1 ] = '\0';
	mFactory = factory;

	mPort = port;

	mManager = NULL;
	mProcList = NULL;

	mPool = new SP_ProcPool( -1 );

	mMaxProc = 128;

	mIsStop = 1;
}

SP_ProcLFServer :: ~SP_ProcLFServer()
{
	if( NULL != mPool ) mPool->dump();

	if( NULL != mManager ) delete mManager;
	mManager = NULL;

	if( NULL != mProcList ) delete mProcList;
	mProcList = NULL;
}

void SP_ProcLFServer :: setMaxProc( int maxProc )
{
	mMaxProc = maxProc;
}

SP_ProcPool * SP_ProcLFServer :: getProcPool()
{
	return mPool;
}

int SP_ProcLFServer :: isStop()
{
	return mIsStop;
}

void SP_ProcLFServer :: shutdown()
{
	mIsStop = 1;
}

int SP_ProcLFServer :: run()
{
	pthread_attr_t attr;
	pthread_attr_init( &attr );
	assert( pthread_attr_setstacksize( &attr, 1024 * 1024 ) == 0 );
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );

	pthread_t thread = 0;
	int ret = pthread_create( &thread, &attr, reinterpret_cast<void*(*)(void*)>(monitorThread), this );
	pthread_attr_destroy( &attr );
	if( 0 == ret ) {
		syslog( LOG_NOTICE, "Thread #%ld has been created to monitor on port [%d]", thread, mPort );
	} else {
		syslog( LOG_WARNING, "Unable to create a thread for TCP server on port [%d], %s",
			mPort, strerror( errno ) ) ;
		ret = -1;
	}

	return ret;
}

void SP_ProcLFServer :: runForever()
{
	monitorThread( this );
}

void * SP_ProcLFServer :: monitorThread( void * arg )
{
	((SP_ProcLFServer*)arg)->start();

	return NULL;
}

int SP_ProcLFServer :: start()
{
	/* Don't die with SIGPIPE on remote read shutdown. That's dumb. */
	signal( SIGPIPE, SIG_IGN );
	signal( SIGCHLD, SIG_IGN );

	int listenfd = -1;
	assert( 0 == SP_ProcPduUtils::tcp_listen( mBindIP, mPort, &listenfd ) );

	SP_ProcWorkerFactoryLFAdapter * factory =
			new SP_ProcWorkerFactoryLFAdapter( listenfd, mFactory );
	factory->setMaxRequestsPerProc( mPool->getMaxRequestsPerProc() );

	mManager = new SP_ProcManager( factory );
	mManager->start();

	delete mPool;
	mPool = mManager->getProcPool();
	mProcList = new SP_ProcInfoList();

	close( listenfd );

	for( int i = 0; i < mMaxProc; i++ ) {
		SP_ProcInfo * info = mPool->get();
		if( NULL != info ) {
			mProcList->append( info );
		} else {
			syslog( LOG_WARNING, "WARN: Create proc fail, only %d proc service",
					mProcList->getCount() );
			break;
		}
	}

	mIsStop = 0;

	for( ; 0 == mIsStop ; ) {
		fd_set rset;
		FD_ZERO( &rset );

		int maxfd = 0;

		for( int i = 0; i < mProcList->getCount(); i++ ) {
			const SP_ProcInfo * iter = mProcList->getItem( i );
			FD_SET( iter->getPipeFd(), &rset );
			maxfd = maxfd > iter->getPipeFd() ? maxfd : iter->getPipeFd();
		}

		int nsel = select( maxfd + 1, &rset, NULL, NULL, NULL );

		/* find any exception children */
		for( int i = mProcList->getCount() - 1; i >= 0; i-- ) {
			const SP_ProcInfo * iter = mProcList->getItem( i );
			if( FD_ISSET( iter->getPipeFd(), &rset ) ) {
				syslog( LOG_WARNING, "WARN: proc #%u dead", iter->getPid() );

				SP_ProcInfo * info = mProcList->takeItem( i );
				mPool->erase( info );

				if (--nsel == 0) break;	/* all done with select() results */
			}
		}

		for( int i = mProcList->getCount(); i < mMaxProc; i++ ) {
			SP_ProcInfo * info = mPool->get();
			if( NULL != info ) {
				mProcList->append( info );
			} else {
				syslog( LOG_WARNING, "WARN: Create proc fail, only %d proc service",
						mProcList->getCount() );
				break;
			}
		}
	}

	return 0;
}

