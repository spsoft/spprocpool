/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <sys/poll.h>
#include <sys/time.h>

#include "spprocdatum.hpp"

#include "spprocmanager.hpp"
#include "spprocpool.hpp"
#include "spprocpdu.hpp"

SP_ProcDatumHandler :: ~SP_ProcDatumHandler()
{
}

//-------------------------------------------------------------------

SP_ProcDatumService :: ~SP_ProcDatumService()
{
}

//-------------------------------------------------------------------

SP_ProcDatumServiceFactory :: ~SP_ProcDatumServiceFactory()
{
}

void SP_ProcDatumServiceFactory :: workerInit( const SP_ProcInfo * procInfo )
{
}

void SP_ProcDatumServiceFactory :: workerEnd( const SP_ProcInfo * procInfo )
{
}

//-------------------------------------------------------------------

class SP_ProcWorkerDatumAdapter : public SP_ProcWorker {
public:
	SP_ProcWorkerDatumAdapter( SP_ProcDatumServiceFactory * factory );
	virtual ~SP_ProcWorkerDatumAdapter();

	virtual void process( SP_ProcInfo * procInfo );

private:
	SP_ProcDatumServiceFactory * mFactory;
};

SP_ProcWorkerDatumAdapter :: SP_ProcWorkerDatumAdapter( SP_ProcDatumServiceFactory * factory )
{
	mFactory = factory;
}

SP_ProcWorkerDatumAdapter :: ~SP_ProcWorkerDatumAdapter()
{
}

void SP_ProcWorkerDatumAdapter :: process( SP_ProcInfo * procInfo )
{
	mFactory->workerInit( procInfo );

	for( ; ; ) {
		SP_ProcDataBlock request;
		SP_ProcPdu_t pdu;
		memset( &pdu, 0, sizeof( pdu ) );

		if( SP_ProcPduUtils::read_pdu( procInfo->getPipeFd(), &pdu, &request ) > 0 ) {
			SP_ProcDataBlock reply;

			SP_ProcDatumService * service = mFactory->create();
			service->handle( &request, &reply );
			delete service;

			SP_ProcPdu_t replyPdu;
			memset( &replyPdu, 0, sizeof( SP_ProcPdu_t ) );
			replyPdu.mMagicNum = SP_ProcPdu_t::MAGIC_NUM;
			replyPdu.mSrcPid = getpid();
			replyPdu.mDestPid = pdu.mSrcPid;
			replyPdu.mDataSize = reply.getDataSize();

			if( SP_ProcPduUtils::send_pdu( procInfo->getPipeFd(), &replyPdu, reply.getData() ) < 0 ) {
				break;
			}
		} else {
			break;
		}
	}

	mFactory->workerEnd( procInfo );
}

//-------------------------------------------------------------------

class SP_ProcWorkerFactoryDatumAdapter : public SP_ProcWorkerFactory {
public:
	SP_ProcWorkerFactoryDatumAdapter( SP_ProcDatumServiceFactory * factory );
	virtual ~SP_ProcWorkerFactoryDatumAdapter();

	virtual SP_ProcWorker * create() const;

private:
	SP_ProcDatumServiceFactory * mFactory;
};

SP_ProcWorkerFactoryDatumAdapter :: SP_ProcWorkerFactoryDatumAdapter(
		SP_ProcDatumServiceFactory * factory )
{
	mFactory = factory;
}

SP_ProcWorkerFactoryDatumAdapter :: ~SP_ProcWorkerFactoryDatumAdapter()
{
	delete mFactory;
	mFactory = NULL;
}

SP_ProcWorker * SP_ProcWorkerFactoryDatumAdapter :: create() const
{
	return new SP_ProcWorkerDatumAdapter( mFactory );
}

//-------------------------------------------------------------------

class SP_ProcInfoListEx {
public:
	SP_ProcInfoListEx();
	~SP_ProcInfoListEx();

	void append( SP_ProcInfo * info );
	SP_ProcInfo * takeByPipeFd( int pipeFd );

	int conv2pollfd( struct pollfd pfd[], int nfds );

	int getCount() const;

private:
	pthread_mutex_t mMutex;
	pthread_cond_t mCond;
	pthread_cond_t mEmptyCond;

	SP_ProcInfoList * mList;
};

SP_ProcInfoListEx :: SP_ProcInfoListEx()
{
	pthread_mutex_init( &mMutex, NULL );
	pthread_cond_init( &mCond, NULL );
	pthread_cond_init( &mEmptyCond, NULL );

	mList = new SP_ProcInfoList();
}

SP_ProcInfoListEx :: ~SP_ProcInfoListEx()
{
	pthread_mutex_destroy( &mMutex );
	pthread_cond_destroy( &mCond );
	pthread_cond_destroy( &mEmptyCond );

	delete mList;
	mList = NULL;
}

void SP_ProcInfoListEx :: append( SP_ProcInfo * info )
{
	pthread_mutex_lock( &mMutex );

	mList->append( info );

	if( 1 == mList->getCount() ) pthread_cond_signal( &mCond );

	pthread_mutex_unlock( &mMutex );
}

SP_ProcInfo * SP_ProcInfoListEx :: takeByPipeFd( int pipeFd )
{
	SP_ProcInfo * ret = NULL;

	pthread_mutex_lock( &mMutex );
	ret = mList->takeItem( mList->findByPipeFd( pipeFd ) );
	if( mList->getCount() <= 0 ) pthread_cond_signal( &mEmptyCond );

	pthread_mutex_unlock( &mMutex );

	return ret;
}

int SP_ProcInfoListEx :: conv2pollfd( struct pollfd pfd[], int nfds )
{
	pthread_mutex_lock( &mMutex );

	if( mList->getCount() <= 0 ) {
		syslog( LOG_INFO, "INFO: waiting for not empty" );

		struct timezone tz;
		struct timeval now;
		gettimeofday( &now, &tz );

		struct timespec timeout;
		timeout.tv_sec = now.tv_sec + 5;
		timeout.tv_nsec = now.tv_usec * 1000;

		pthread_cond_timedwait( &mCond, &mMutex, &timeout );
	}

	nfds = mList->getCount() > nfds ? nfds : mList->getCount();

	for( int i = 0; i < nfds; i++ ) {
		const SP_ProcInfo * info = mList->getItem( i );

		pfd[i].fd = info->getPipeFd();
		pfd[i].events = POLLIN;
		pfd[i].revents = 0;
	}

	pthread_mutex_unlock( &mMutex );

	return nfds;
}

int SP_ProcInfoListEx :: getCount() const
{
	return mList->getCount();
}

//-------------------------------------------------------------------

SP_ProcDatumDispatcher :: SP_ProcDatumDispatcher( SP_ProcDatumServiceFactory * factory,
		SP_ProcDatumHandler * handler )
{
	mHandler = handler;

	mManager = new SP_ProcManager( new SP_ProcWorkerFactoryDatumAdapter( factory ) );
	mManager->start();

	mPool = mManager->getProcPool();

	mIsStop = 0;

	mBusyList = new SP_ProcInfoListEx();

	mMaxProc = 128;

	pthread_mutex_init( &mMutex, NULL );
	pthread_cond_init( &mCond, NULL );

	pthread_attr_t attr;
	pthread_attr_init( &attr );
	assert( pthread_attr_setstacksize( &attr, 1024 * 1024 ) == 0 );
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );

	pthread_t thread = 0;
	int ret = pthread_create( &thread, &attr, reinterpret_cast<void*(*)(void*)>(checkReply), this );
	pthread_attr_destroy( &attr );
	if( 0 == ret ) {
		syslog( LOG_NOTICE, "Thread #%ld has been created to check reply", thread );
	} else {
		syslog( LOG_WARNING, "Unable to create a thread to check reply, errno %d, %s",
				errno, strerror( errno ) );
	}
}

SP_ProcDatumDispatcher :: ~SP_ProcDatumDispatcher()
{
	pthread_mutex_lock( &mMutex );
	mIsStop = 1;
	pthread_cond_wait( &mCond, &mMutex );
	pthread_mutex_unlock( &mMutex );

	delete mHandler;
	mHandler = NULL;

	delete mManager;
	mManager = NULL;

	delete mBusyList;
	mBusyList = NULL;
}

SP_ProcPool * SP_ProcDatumDispatcher :: getProcPool()
{
	return mPool;
}

void SP_ProcDatumDispatcher :: setMaxProc( int maxProc )
{
	mMaxProc = maxProc;
}

void * SP_ProcDatumDispatcher :: checkReply( void * args )
{
	SP_ProcDatumDispatcher * dispatcher = (SP_ProcDatumDispatcher*)args;

	SP_ProcInfoListEx * list = dispatcher->mBusyList;
	SP_ProcDatumHandler * handler = dispatcher->mHandler;
	SP_ProcPool * pool = dispatcher->mPool;

	for( ; ! ( dispatcher->mIsStop && 0 == list->getCount() ); ) {
		// 1. collect fd
		static int SP_PROC_MAX_FD = 1024;
		struct pollfd pfd[ SP_PROC_MAX_FD ];
		int nfds = list->conv2pollfd( pfd, SP_PROC_MAX_FD );

		if( 0 == nfds ) continue;

		// 2. select fd
		int ret = -1;
		for( int retry = 0; retry < 2; retry++ ) {
			ret = poll( pfd, nfds, 10 );
			if( -1 == ret && EINTR == errno ) continue;
			break;
		}

		// 3. dispatch reply/error
		if( ret > 0 ) {
			for( int i = 0; i < nfds; i++ ) {
				if( 0 == pfd[i].revents ) {
					// timeout, wait for next turn
				} else {
					SP_ProcInfo * info = list->takeByPipeFd( pfd[i].fd );
					if( NULL == info ) {
						syslog( LOG_CRIT, "CRIT: found a not exists fd %d, dangerous", pfd[i].fd );
						continue;
					}

					SP_ProcPdu_t pdu;
					SP_ProcDataBlock reply;

					if( pfd[i].revents & POLLIN ) {
						// readable
						if( SP_ProcPduUtils::read_pdu( pfd[i].fd, &pdu, &reply ) > 0 ) {
							handler->onReply( info->getPid(), &reply );
							pool->save( info );
						} else {
							pool->erase( info );
						}
					} else {
						// error
						handler->onError( info->getPid() );
						pool->erase( info );
					}
				}
			}
		} else {
			// ignore
		}
	}

	pthread_mutex_lock( &( dispatcher->mMutex ) );
	pthread_cond_signal( &( dispatcher->mCond ) );
	pthread_mutex_unlock( &( dispatcher->mMutex ) );

	return NULL;
}

pid_t SP_ProcDatumDispatcher :: dispatch( const void * request, size_t len )
{
	if( mBusyList->getCount() >= mMaxProc ) return -1;

	pid_t ret = -1;

	SP_ProcInfo * info = mPool->get();
	if( NULL != info ) {
		SP_ProcPdu_t pdu;
		memset( &pdu, 0, sizeof( pdu ) );
		pdu.mMagicNum = SP_ProcPdu_t::MAGIC_NUM;
		pdu.mSrcPid = getpid();
		pdu.mDestPid = info->getPid();
		pdu.mDataSize = len;

		if( SP_ProcPduUtils::send_pdu( info->getPipeFd(), &pdu, request ) > 0 ) {
			ret = info->getPid();
			mBusyList->append( info );
		} else {
			mPool->erase( info );
		}
	}

	return ret;
}

void SP_ProcDatumDispatcher :: dump() const
{
	mPool->dump();
}

