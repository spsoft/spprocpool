/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#ifndef __spprocpool_hpp__
#define __spprocpool_hpp__

#include <sys/types.h>
#include <pthread.h>
#include <time.h>

class SP_ProcInfo {
public:
	static const char CHAR_BUSY;
	static const char CHAR_IDLE;
	static const char CHAR_EXIT;

	SP_ProcInfo( int pipeFd );
	~SP_ProcInfo();

	void setPid( pid_t pid );
	pid_t getPid() const;

	int getPipeFd() const;

	void setRequests( int requests );
	int getRequests() const;

	void setLastActiveTime( time_t lastActiveTime );
	time_t getLastActiveTime() const;

	void setIdle( int idle );
	int isIdle() const;

	void dump() const;

private:
	int mPipeFd;
	pid_t mPid;

	int mRequests;
	time_t mLastActiveTime;
	char mIsIdle;
};

class SP_ProcInfoList {
public:
	SP_ProcInfoList();
	~SP_ProcInfoList();

	int getCount() const;

	void append( SP_ProcInfo * info );
	SP_ProcInfo * getItem( int index ) const;
	SP_ProcInfo * takeItem( int index );

	int findByPid( pid_t pid ) const;
	int findByPipeFd( int pipeFd ) const;

	void dump() const;

private:
	SP_ProcInfo ** mList;
	int mMaxCount;
	int mCount;
};

class SP_ProcPool {
public:
	SP_ProcPool( int mgrPipe );
	~SP_ProcPool();

	// default is 0, unlimited
	void setMaxRequestsPerProc( int maxRequestsPerProc );
	int getMaxRequestsPerProc() const;

	// default is 0, unlimited
	void setMaxIdleProc( int maxIdleProc );

	int ensureIdleProc( int idleCount );

	int getIdleCount();

	SP_ProcInfo * get();

	void save( SP_ProcInfo * procInfo );

	void erase( SP_ProcInfo * procInfo );

	void dump() const;

private:

	SP_ProcInfo * create();

	// pipes to communicate between process manager and app
	int mMgrPipe;

	SP_ProcInfoList * mList;
	pthread_mutex_t mMutex;

	int mMaxRequestsPerProc, mMaxIdleProc;
};

#endif

