/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#ifndef __spproclfsvr_hpp__ 
#define __spproclfsvr_hpp__

class SP_ProcInetServiceFactory;
class SP_ProcManager;
class SP_ProcPool;
class SP_ProcInfoList;

class SP_ProcLFServer {
public:
	SP_ProcLFServer( const char * bindIP, int port,
			SP_ProcInetServiceFactory * factory );
	~SP_ProcLFServer();

	// default is 128
	void setMaxProc( int maxProc );

	SP_ProcPool * getProcPool();

	int isStop();

	void shutdown();

	int run();
	void runForever();

private:
	char mBindIP[ 64 ];
	int mPort;

	// manager side
	SP_ProcManager * mManager;
	SP_ProcInetServiceFactory * mFactory;

	// app side
	SP_ProcPool * mPool;

	SP_ProcInfoList * mProcList;

	int mIsStop;

	int mMaxProc;

	static void * monitorThread( void * arg );

	int start();
};

#endif

