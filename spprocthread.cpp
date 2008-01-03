/*
 * Copyright 2007-2008 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <syslog.h>

#include "spprocthread.hpp"

typedef struct tagSP_ProcThread {
	pthread_t mId;
	pthread_mutex_t mMutex;
	pthread_cond_t mCond;
	SP_ProcThreadPool::DispatchFunc_t mFunc;
	void * mArg;
	SP_ProcThreadPool * mParent;
} SP_ProcThread_t;

SP_ProcThreadPool :: SP_ProcThreadPool( int maxThreads, const char * tag )
{
	if( maxThreads <= 0 ) maxThreads = 2;

	pthread_mutex_init( &mMainMutex, NULL );
	pthread_cond_init( &mIdleCond, NULL );
	pthread_cond_init( &mFullCond, NULL );
	pthread_cond_init( &mEmptyCond, NULL );
	mMaxThreads = maxThreads;
	mIndex = 0;
	mIsShutdown = 0;
	mTotal = 0;

	tag = NULL == tag ? "unknown" : tag;
	mTag = strdup( tag );

	mThreadList = ( SP_ProcThread_t ** )malloc( sizeof( void * ) * mMaxThreads );
	memset( mThreadList, 0, sizeof( void * ) * mMaxThreads );

	mFullCallback = NULL;
	mFullCallbackArg = NULL;
}

SP_ProcThreadPool :: ~SP_ProcThreadPool()
{
	pthread_mutex_lock( &mMainMutex );

	if( mIndex < mTotal ) {
		syslog( LOG_DEBUG, "[tp@%s] waiting for %d thread(s) to finish\n", mTag, mTotal - mIndex );
		pthread_cond_wait( &mFullCond, &mMainMutex );
	}

	mIsShutdown = 1;

	for( int i = 0; i < mIndex; i++ ) {
		SP_ProcThread_t * thread = mThreadList[ i ];
		pthread_mutex_lock( &thread->mMutex );
		pthread_cond_signal( &thread->mCond ) ;
		pthread_mutex_unlock ( &thread->mMutex );
	}

	if( mTotal > 0 ) {
		syslog( LOG_DEBUG, "[tp@%s] waiting for %d thread(s) to exit\n", mTag, mTotal );
		pthread_cond_wait( &mEmptyCond, &mMainMutex );
	}

	syslog( LOG_DEBUG, "[tp@%s] destroy %d thread structure(s)\n", mTag, mIndex );
	for( int i = 0; i < mIndex; i++ ) {
		SP_ProcThread_t * thread = mThreadList[ i ];
		pthread_mutex_destroy( &thread->mMutex );
		pthread_cond_destroy( &thread->mCond );
		free( thread );
		mThreadList[ i ] = NULL;
	}

	pthread_mutex_unlock( &mMainMutex );

	mIndex = 0;

	pthread_mutex_destroy( &mMainMutex );
	pthread_cond_destroy( &mIdleCond );
	pthread_cond_destroy( &mFullCond );
	pthread_cond_destroy( &mEmptyCond );

	free( mThreadList );
	mThreadList = NULL;

	free( mTag );
	mTag = NULL;
}

void SP_ProcThreadPool :: setFullCallback( DispatchFunc_t fullCallback, void * arg )
{
	mFullCallback = fullCallback;
	mFullCallbackArg = arg;
}

int SP_ProcThreadPool :: getMaxThreads()
{
	return mMaxThreads;
}

int SP_ProcThreadPool :: dispatch( DispatchFunc_t dispatchFunc, void *arg )
{
	int ret = 0;

	pthread_attr_t attr;
	SP_ProcThread_t * thread = NULL;

	pthread_mutex_lock( &mMainMutex );

	if( mIndex <= 0 && mTotal >= mMaxThreads ) {
		pthread_cond_wait( &mIdleCond, &mMainMutex );
	}

	if( mIndex <= 0 ) {
		SP_ProcThread_t * thread = ( SP_ProcThread_t * )malloc( sizeof( SP_ProcThread_t ) );
		thread->mId = 0;
		pthread_mutex_init( &thread->mMutex, NULL );
		pthread_cond_init( &thread->mCond, NULL );
		thread->mFunc = dispatchFunc;
		thread->mArg = arg;
		thread->mParent = this;

		pthread_attr_init( &attr );
		pthread_attr_setdetachstate( &attr,PTHREAD_CREATE_DETACHED );

		if( 0 == pthread_create( &( thread->mId ), &attr, wrapperFunc, thread ) ) {
			mTotal++;
			syslog( LOG_DEBUG, "[tp@%s] create thread#%ld\n", mTag, thread->mId );
		} else {
			ret = -1;
			syslog( LOG_WARNING, "[tp@%s] cannot create thread\n", mTag );
			pthread_mutex_destroy( &thread->mMutex );
			pthread_cond_destroy( &thread->mCond );
			free( thread );
		}
		pthread_attr_destroy( &attr );
	} else {
		mIndex--;
		thread = mThreadList[ mIndex ];
		mThreadList[ mIndex ] = NULL;

		thread->mFunc = dispatchFunc;
		thread->mArg = arg;
		thread->mParent = this;

		pthread_mutex_lock( &thread->mMutex );
		pthread_cond_signal( &thread->mCond ) ;
		pthread_mutex_unlock ( &thread->mMutex );
	}

	pthread_mutex_unlock( &mMainMutex );

	return ret;
}

void SP_ProcThreadPool :: wait4idler()
{
	if( mIndex <= 0 && mTotal >= mMaxThreads ) {
		pthread_mutex_lock( &mMainMutex );
		if( mIndex <= 0 && mTotal >= mMaxThreads ) {
			pthread_cond_wait( &mIdleCond, &mMainMutex );
		}
		pthread_mutex_unlock( &mMainMutex );
	}
}

void * SP_ProcThreadPool :: wrapperFunc( void * arg )
{
	SP_ProcThread_t * thread = ( SP_ProcThread_t * )arg;

	for( ; 0 == thread->mParent->mIsShutdown; ) {
		thread->mFunc( thread->mArg );

		pthread_mutex_lock( &thread->mMutex );
		if( 0 == thread->mParent->saveThread( thread ) ) {
			pthread_cond_wait( &thread->mCond, &thread->mMutex );
			pthread_mutex_unlock( &thread->mMutex );
		} else {
			pthread_mutex_unlock( &thread->mMutex );
			pthread_cond_destroy( &thread->mCond );
			pthread_mutex_destroy( &thread->mMutex );

			free( thread );
			thread = NULL;
			break;
		}
	}

	if( NULL != thread ) {
		pthread_mutex_lock( &thread->mParent->mMainMutex );
		thread->mParent->mTotal--;
		if( thread->mParent->mTotal <= 0 ) {
			pthread_cond_signal( &thread->mParent->mEmptyCond );
		}
		pthread_mutex_unlock( &thread->mParent->mMainMutex );
	}

	return NULL;
}

int SP_ProcThreadPool :: saveThread( SP_ProcThread_t * thread )
{
	int ret = -1;

	pthread_mutex_lock( &mMainMutex );

	if( mIndex < mMaxThreads ) {
		mThreadList[ mIndex ] = thread;
		mIndex++;
		ret = 0;

		pthread_cond_signal( &mIdleCond );

		if( mIndex >= mTotal ) {
			if( NULL != mFullCallback ) mFullCallback( mFullCallbackArg );
			pthread_cond_signal( &mFullCond );
		}
	}

	pthread_mutex_unlock( &mMainMutex );

	return ret;
}

