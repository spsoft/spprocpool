/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>

#include "spproclock.hpp"

SP_ProcLock :: ~SP_ProcLock()
{
}

//-------------------------------------------------------------------

SP_ProcFileLock :: SP_ProcFileLock()
{
	mFd = -1;
}

SP_ProcFileLock :: ~SP_ProcFileLock()
{
	if( mFd >= 0 ) close( mFd );
	mFd = -1;
}

int SP_ProcFileLock :: lock()
{
	if( mFd < 0 ) return -1;

	struct flock lock_it;

	lock_it.l_type = F_WRLCK;
	lock_it.l_whence = SEEK_SET;
	lock_it.l_start = 0;
	lock_it.l_len = 0;

	int rc = -1;

	while( ( rc = fcntl( mFd, F_SETLKW, &lock_it ) ) < 0 ) {
		if( errno == EINTR ) {
			continue;
		} else {
			break;
		}
	}

	return rc;
}

int SP_ProcFileLock :: unlock()
{
	if( mFd < 0 ) return -1;

	struct flock unlock_it;

	unlock_it.l_type = F_UNLCK;
	unlock_it.l_whence = SEEK_SET;
	unlock_it.l_start = 0;
	unlock_it.l_len = 0;

	return fcntl( mFd, F_SETLKW, &unlock_it );
}

int SP_ProcFileLock :: init( const char * filePath )
{
	mFd = open( filePath, O_CREAT | O_RDWR, 0600 );

	if( mFd < 0 ) {
		syslog( LOG_WARNING, "WARN: open %s fail, errno %d, %s",
				filePath, errno, strerror( errno ) );
	}

	return mFd >= 0 ? 0 : -1;
}

//-------------------------------------------------------------------

SP_ProcThreadLock :: SP_ProcThreadLock()
{
	mMutex = NULL;

#ifdef _POSIX_THREAD_PROCESS_SHARED
	int fd = open("/dev/zero", O_RDWR, 0);
	if( fd >= 0 ) {
		mMutex = (pthread_mutex_t*)mmap( 0, sizeof(pthread_mutex_t),
			PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
		close( fd );

		assert( NULL != mMutex );

		pthread_mutexattr_t mattr;
		pthread_mutexattr_init( &mattr );
		assert( 0 == pthread_mutexattr_setpshared( &mattr, PTHREAD_PROCESS_SHARED ) );
		assert( 0 == pthread_mutex_init( mMutex, &mattr ) );
	} else {
		syslog( LOG_WARNING, "WARN: open /dev/zero fail, errno %d, %s",
				errno, strerror( errno ) );
	}
#endif
}

SP_ProcThreadLock :: ~SP_ProcThreadLock()
{
	if( NULL != mMutex ) {
		pthread_mutex_destroy( mMutex );
		assert( 0 == munmap( mMutex, sizeof( pthread_mutex_t ) ) );
	}

	mMutex = NULL;
}

int SP_ProcThreadLock :: lock()
{
#ifdef _POSIX_THREAD_PROCESS_SHARED
	if( NULL == mMutex ) return -1;

	return pthread_mutex_lock( mMutex );
#endif

	return 0;
}

int SP_ProcThreadLock :: unlock()
{
#ifdef _POSIX_THREAD_PROCESS_SHARED
	if( NULL == mMutex ) return -1;

	return pthread_mutex_unlock( mMutex );
#endif

	return 0;
}

