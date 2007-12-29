/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#ifndef __spproclock_hpp__
#define __spproclock_hpp__

#include <pthread.h>

class SP_ProcLock {
public:
	virtual ~SP_ProcLock();

	// 0 : OK, -1 : Fail
	virtual int lock() = 0;

	// 0 : OK, -1 : Fail
	virtual int unlock() = 0;
};

class SP_ProcFileLock : public SP_ProcLock {
public:
	SP_ProcFileLock();

	virtual ~SP_ProcFileLock();

	virtual int lock();

	virtual int unlock();

	// 0 : OK, -1 : Fail
	int init( const char * filePath );

private:
	int mFd;
};

class SP_ProcThreadLock : public SP_ProcLock {
public:
	SP_ProcThreadLock();
	virtual ~SP_ProcThreadLock();

	virtual int lock();

	virtual int unlock();

private:
	pthread_mutex_t * mMutex;
};

#endif

