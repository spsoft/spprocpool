/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#ifndef __spprocinet_hpp__
#define __spprocinet_hpp__

class SP_ProcManager;
class SP_ProcPool;
class SP_ProcInfoList;
class SP_ProcInfo;

class SP_ProcInetService {
public:
	virtual ~SP_ProcInetService();

	virtual void handle( int socketFd ) = 0;
};

class SP_ProcInetServiceFactory {
public:
	virtual ~SP_ProcInetServiceFactory();

	virtual SP_ProcInetService * create() const = 0;

	virtual void workerInit( const SP_ProcInfo * procInfo );

	virtual void workerEnd( const SP_ProcInfo * procInfo );
};

class SP_ProcInetServer {
public:
	SP_ProcInetServer( const char * bindIP, int port,
			SP_ProcInetServiceFactory * factory );
	~SP_ProcInetServer();

	// default is 
	void setMaxProc( int maxProc );

	void setMaxRequestsPerProc( int maxRequestsPerProc );

	void setMaxIdleProc( int maxIdleProc );

	void setMinIdleProc( int minIdleProc );

	int start();

	int isStop();

	void shutdown();

private:
	char mBindIP[ 64 ];
	int mPort;

	SP_ProcInetServiceFactory * mFactory;

	int mIsStop;
	int mMaxProc, mMaxRequestsPerProc, mMaxIdleProc, mMinIdleProc;
};

#endif

