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

typedef struct tagSP_ProcArgs {
	int mMaxProc;
	int mMaxIdleProc;
	int mMinIdleProc;
} SP_ProcArgs_t;

class SP_ProcBaseServer {
public:
	SP_ProcBaseServer( const char * bindIP, int port,
			SP_ProcInetServiceFactory * factory );
	virtual ~SP_ProcBaseServer();

	void setArgs( const SP_ProcArgs_t * args );

	void getArgs( SP_ProcArgs_t * args ) const;

	void setMaxRequestsPerProc( int maxRequestsPerProc );

	int isStop();

	void shutdown();

	virtual int start() = 0;

protected:

	char mBindIP[ 64 ];
	int mPort;

	SP_ProcInetServiceFactory * mFactory;

	int mIsStop;
	SP_ProcArgs_t * mArgs;
	int mMaxRequestsPerProc;
};

class SP_ProcInetServer : public SP_ProcBaseServer {
public:
	SP_ProcInetServer( const char * bindIP, int port,
			SP_ProcInetServiceFactory * factory );
	virtual ~SP_ProcInetServer();

	virtual int start();
};

#endif

