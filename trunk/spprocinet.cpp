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

//-------------------------------------------------------------------

class SP_ProcWorkerInetAdapter : public SP_ProcWorker {
public:
	SP_ProcWorkerInetAdapter( SP_ProcInetService * service );
	~SP_ProcWorkerInetAdapter();

	virtual void process( const SP_ProcInfo * procInfo );

private:
	SP_ProcInetService * mService;
};

SP_ProcWorkerInetAdapter :: SP_ProcWorkerInetAdapter( SP_ProcInetService * service )
{
	mService = service;
}

SP_ProcWorkerInetAdapter :: ~SP_ProcWorkerInetAdapter()
{
	delete mService;
	mService = NULL;
}

void SP_ProcWorkerInetAdapter :: process( const SP_ProcInfo * procInfo )
{
	for( ; ; ) {
		int fd = SP_ProcPduUtils::recv_fd( procInfo->getPipeFd() );
		if( fd >= 0 ) {
			mService->handle( fd );
			close( fd );

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
	return new SP_ProcWorkerInetAdapter( mFactory->create() );
}

//-------------------------------------------------------------------

SP_ProcInetServer :: SP_ProcInetServer( const char * bindIP, int port,
		SP_ProcInetServiceFactory * factory )
{
	strncpy( mBindIP, bindIP, sizeof( mBindIP ) );
	mBindIP[ sizeof( mBindIP ) - 1 ] = '\0';

	mPort = port;

	mManager = new SP_ProcManager( new SP_ProcWorkerFactoryInetAdapter( factory ) );
	mManager->start();

	mPool = mManager->getProcPool();

	mBusyList = new SP_ProcInfoList();

	mMaxProc = 128;

	mIsStop = 1;
}

SP_ProcInetServer :: ~SP_ProcInetServer()
{
	mPool->dump();

	delete mManager;
	mManager = NULL;

	delete mBusyList;
	mBusyList = NULL;
}

void SP_ProcInetServer :: setMaxProc( int maxProc )
{
	mMaxProc = maxProc;
}

SP_ProcPool * SP_ProcInetServer :: getProcPool()
{
	return mPool;
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
	int ret = SP_ProcPduUtils::tcp_listen( mBindIP, mPort, &listenfd );

	if( 0 != ret ) return -1;

	mIsStop = 0;

	for ( ; 0 == mIsStop; ) {
		fd_set rset;
		FD_ZERO( &rset );

		FD_SET( listenfd, &rset );
		int maxfd = listenfd;

		for( int i = 0; i < mBusyList->getCount(); i++ ) {
			const SP_ProcInfo * iter = mBusyList->getItem( i );
			FD_SET( iter->getPipeFd(), &rset );
			maxfd = maxfd > iter->getPipeFd() ? maxfd : iter->getPipeFd();
		}

		if( mBusyList->getCount() >= mMaxProc ) FD_CLR( listenfd, &rset );

		int nsel = select( maxfd + 1, &rset, NULL, NULL, NULL );

		/* check for new connections */
		if( FD_ISSET( listenfd, &rset ) && mBusyList->getCount() < mMaxProc ) {
			SP_ProcInfo * info = mPool->get();

			if( NULL != info ) {
				struct sockaddr_in clientAddr;
				socklen_t clientLen = sizeof( clientAddr );
				int clientFd = accept( listenfd, (struct sockaddr *)&clientAddr, &clientLen );

				if( clientFd >= 0 ) {
					if( 0 == SP_ProcPduUtils::send_fd( info->getPipeFd(), clientFd ) ) {
						mBusyList->append( info );
					} else {
						mPool->erase( info );
					}
					close( clientFd );
				} else {
					mPool->save( info );
				}
			}

			if (--nsel == 0) continue;	/* all done with select() results */
		}

		/* find any newly-available children */
		for( int i = mBusyList->getCount() - 1; i >= 0; i-- ) {
			const SP_ProcInfo * iter = mBusyList->getItem( i );
			if( FD_ISSET( iter->getPipeFd(), &rset ) ) {
				SP_ProcInfo * info = mBusyList->takeItem( i );

				SP_ProcPdu_t pdu;
				if( SP_ProcPduUtils::read_pdu( iter->getPipeFd(), &pdu, NULL ) > 0 ) {
					assert( info->getPid() == pdu.mSrcPid );
					mPool->save( info );
				} else {
					mPool->erase( info );
				}

				if (--nsel == 0) break;	/* all done with select() results */
			}
		}
	}

	close( listenfd );

	return 0;
}

void * SP_ProcInetServer :: acceptThread( void * arg )
{
	((SP_ProcInetServer*)arg)->start();

	return NULL;
}

int SP_ProcInetServer :: run()
{
	pthread_attr_t attr;
	pthread_attr_init( &attr );
	assert( pthread_attr_setstacksize( &attr, 1024 * 1024 ) == 0 );
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );

	pthread_t thread = 0;
	int ret = pthread_create( &thread, &attr, reinterpret_cast<void*(*)(void*)>(acceptThread), this );
	pthread_attr_destroy( &attr );
	if( 0 == ret ) {
		syslog( LOG_NOTICE, "Thread #%ld has been created to listen on port [%d]", thread, mPort );
	} else {
		syslog( LOG_WARNING, "Unable to create a thread for TCP server on port [%d], %s",
			mPort, strerror( errno ) ) ;
		ret = -1;
	}

	return ret;
}

void SP_ProcInetServer :: runForever()
{
	acceptThread( this );
}

