/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "spprocpool.hpp"
#include "spprocpdu.hpp"

SP_ProcInfo :: SP_ProcInfo( int pipeFd )
{
	mPipeFd = pipeFd;
	mPid = -1;

	mRequests = 0;
	time( &mLastActiveTime );
}

SP_ProcInfo :: ~SP_ProcInfo()
{
	close( mPipeFd );
	mPipeFd = -1;

	mPid = -1;
}

void SP_ProcInfo :: setPid( pid_t pid )
{
	mPid = pid;
}

pid_t SP_ProcInfo :: getPid() const
{
	return mPid;
}

int SP_ProcInfo :: getPipeFd() const
{
	return mPipeFd;
}

void SP_ProcInfo :: setRequests( int requests )
{
	mRequests = requests;
}

int SP_ProcInfo :: getRequests() const
{
	return mRequests;
}

void SP_ProcInfo :: setLastActiveTime( time_t lastActiveTime )
{
	mLastActiveTime = lastActiveTime;
}

time_t SP_ProcInfo :: getLastActiveTime() const
{
	return mLastActiveTime;
}

void SP_ProcInfo :: dump() const
{
	syslog( LOG_INFO, "INFO: pid %d, pipeFd %d, requests %d, lastActiveTime %ld",
		mPid, mPipeFd, mRequests, mLastActiveTime );
}

//-------------------------------------------------------------------

SP_ProcInfoList :: SP_ProcInfoList()
{
	mMaxCount = 8;
	mList = ( SP_ProcInfo ** )malloc( sizeof( SP_ProcInfo * ) * mMaxCount );

	mCount = 0;
}

SP_ProcInfoList :: ~SP_ProcInfoList()
{
	for( int i = 0; i < mCount; i++ ) {
		delete mList[i];
	}

	free( mList );
	mList = NULL;

	mMaxCount = 0;
	mCount = 0;
}

void SP_ProcInfoList :: dump() const
{
	for( int i = 0; i < mCount; i++ ) {
		mList[i]->dump();
	}
}

int SP_ProcInfoList :: getCount() const
{
	return mCount;
}

void SP_ProcInfoList :: append( SP_ProcInfo * info )
{
	assert( NULL != info );

	if( mCount >= mMaxCount ) {
		mMaxCount = ( mMaxCount * 3 ) / 2 + 1;
		mList = (SP_ProcInfo**)realloc( mList, sizeof( void * ) * mMaxCount );
		assert( NULL != mList );
	}

	mList[ mCount++ ] = info;
}

const SP_ProcInfo * SP_ProcInfoList :: getItem( int index ) const
{
	SP_ProcInfo * ret = NULL;

	if( index >= 0 && index < mCount ) ret = mList[ index ];

	return ret;
}

SP_ProcInfo * SP_ProcInfoList :: takeItem( int index )
{
	SP_ProcInfo * ret = NULL;

	if( index >= 0 && index < mCount ) {
		ret = mList[ index ];
		mCount--;

		for( int i = index; i < mCount; i++ ) {
			mList[ i ] = mList[ i + 1 ];
		}
	}

	return ret;
}

int SP_ProcInfoList :: findByPid( pid_t pid ) const
{
	for( int i = 0; i < mCount; i++ ) {
		if( mList[i]->getPid() == pid ) return i;
	}

	return -1;
}

int SP_ProcInfoList :: findByPipeFd( int pipeFd ) const
{
	for( int i = 0; i < mCount; i++ ) {
		if( mList[i]->getPipeFd() == pipeFd ) return i;
	}

	return -1;
}

//-------------------------------------------------------------------

SP_ProcPool :: SP_ProcPool( int mgrPipe )
{
	mMgrPipe = mgrPipe;

	pthread_mutex_init( &mMutex, NULL );
	mList = new SP_ProcInfoList();

	mMaxRequestsPerProc = 0;
	mMaxIdleProc = 0;
}

SP_ProcPool :: ~SP_ProcPool()
{
	close( mMgrPipe );
	mMgrPipe = -1;

	pthread_mutex_destroy( &mMutex );

	delete mList;
	mList = NULL;
}

void SP_ProcPool :: dump() const
{
	pthread_mutex_lock( const_cast<pthread_mutex_t*>(&mMutex) );
	mList->dump();
	pthread_mutex_unlock( const_cast<pthread_mutex_t*>(&mMutex));
}

void SP_ProcPool :: setMaxRequestsPerProc( int maxRequestsPerProc )
{
	mMaxRequestsPerProc = maxRequestsPerProc;
}

void SP_ProcPool :: setMaxIdleProc( int maxIdleProc )
{
	mMaxIdleProc = maxIdleProc;
}

int SP_ProcPool :: initStartProc( int startProc )
{
	SP_ProcInfoList list;
	for( int i = 0; i < startProc; i++ ) {
		SP_ProcInfo * info = get();
		list.append( info );
	}

	for( ; list.getCount() > 0; ) {
		save( list.takeItem( 0 ) );
	}

	return 0;
}

SP_ProcInfo * SP_ProcPool :: get()
{
	SP_ProcInfo * ret = NULL;

	pthread_mutex_lock( &mMutex );

	for( ; NULL == ret && mList->getCount() > 0; ) {
		// get the last one from pool
		ret = mList->takeItem( mList->getCount() - 1);

		if( 0 != kill( ret->getPid(), 0 ) ) {
			syslog( LOG_DEBUG, "DEBUG: process #%d is not exist, remove", ret->getPid() );
			delete ret;
			ret = NULL;
		}
	}

	pthread_mutex_unlock( &mMutex );

	if( NULL == ret ) {
		int pipeFd[ 2 ] = { -1, -1 };
		if( 0 == socketpair( AF_UNIX, SOCK_STREAM, 0, pipeFd ) ) {
			pthread_mutex_lock( &mMutex );
			if( 0 == SP_ProcPduUtils::send_fd( mMgrPipe, pipeFd[0] ) ) {
				SP_ProcPdu_t pdu;
				if( SP_ProcPduUtils::read_pdu( mMgrPipe, &pdu, NULL ) > 0 ) {
					if( pdu.mSrcPid > 0 ) {
						ret = new SP_ProcInfo( pipeFd[1] );
						ret->setPid( pdu.mSrcPid );
					} else {
						pdu.mSrcPid = abs( pdu.mSrcPid );
						syslog( LOG_WARNING, "WARN: cannot create process, errno %d, %s",
								pdu.mSrcPid, strerror( pdu.mSrcPid ) );
					}
				}
				if( NULL == ret ) close( pipeFd[1] );
			} else {
				syslog( LOG_WARNING, "WARN: send fd fail, errno %d, %s",
						errno, strerror( errno ) );
			}
			close( pipeFd[0] );
			pthread_mutex_unlock( &mMutex );
		} else {
			syslog( LOG_WARNING, "socketpair fail, errno %d, %s", errno, strerror( errno ) );
		}
	}

	ret->setRequests( ret->getRequests() + 1 );

	return ret;
}

void SP_ProcPool :: save( SP_ProcInfo * procInfo )
{
	if( mMaxRequestsPerProc > 0 && procInfo->getRequests() >= mMaxRequestsPerProc ) {
		syslog( LOG_DEBUG, "DEBUG: process #%d serve %d requests, remove",
				procInfo->getPid(), procInfo->getRequests() );
		delete procInfo;
	} else {
		pthread_mutex_lock( &mMutex );

		if( mMaxIdleProc > 0 && mList->getCount() >= mMaxIdleProc ) {
			syslog( LOG_DEBUG, "DEBUG: too many idle process, remove process #%d",
					procInfo->getPid() );
			delete procInfo;
		} else {
			syslog( LOG_DEBUG, "DEBUG: save process #%d", procInfo->getPid() );

			procInfo->setLastActiveTime( time( NULL ) );
			mList->append( procInfo );
		}

		pthread_mutex_unlock( &mMutex );
	}
}

void SP_ProcPool :: erase( SP_ProcInfo * procInfo )
{
	syslog( LOG_DEBUG, "DEBUG: erase process #%d", procInfo->getPid() );
	delete procInfo;
}

