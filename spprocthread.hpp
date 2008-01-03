/*
 * Copyright 2007-2008 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#ifndef __spprocthread_hpp__
#define __spprocthread_hpp__

#include <pthread.h>

typedef struct tagSP_ProcThread SP_ProcThread_t;

class SP_ProcThreadPool {
public:
	typedef void ( * DispatchFunc_t )( void * );

	SP_ProcThreadPool( int maxThreads, const char * tag = 0 );
	~SP_ProcThreadPool();

	// set a callback for when all threads are idle
	void setFullCallback( DispatchFunc_t fullCallback, void * arg );

	/// @return 0 : OK, -1 : cannot create thread
	int dispatch( DispatchFunc_t dispatchFunc, void *arg );

	void wait4idler();

	int getMaxThreads();

private:
	char * mTag;

	int mMaxThreads;
	int mIndex;
	int mTotal;
	int mIsShutdown;

	pthread_mutex_t mMainMutex;
	pthread_cond_t mIdleCond;
	pthread_cond_t mFullCond;
	pthread_cond_t mEmptyCond;

	SP_ProcThread_t ** mThreadList;

	DispatchFunc_t mFullCallback;
	void * mFullCallbackArg;

	static void * wrapperFunc( void * );
	int saveThread( SP_ProcThread_t * thread );
};

#endif

