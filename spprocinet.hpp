/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#ifndef __spprocinet_hpp__
#define __spprocinet_hpp__

class SP_ProcManager;
class SP_ProcPool;
class SP_ProcInfoList;

class SP_ProcInetService {
public:
	virtual ~SP_ProcInetService();

	virtual void handle( int socketFd ) = 0;
};

class SP_ProcInetServiceFactory {
public:
	virtual ~SP_ProcInetServiceFactory();

	virtual SP_ProcInetService * create() const = 0;
};

class SP_ProcInetServer {
public:
	SP_ProcInetServer( const char * bindIP, int port,
			SP_ProcInetServiceFactory * factory );
	~SP_ProcInetServer();

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

	// app side
	SP_ProcPool * mPool;

	SP_ProcInfoList * mBusyList;

	int mIsStop;

	int mMaxProc;

	static void * acceptThread( void * arg );

	int start();
};

#endif

