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
class SP_ProcLock;

typedef struct tagSP_ProcArgs SP_ProcArgs_t;

class SP_ProcLFServer {
public:
	static const char CHAR_BUSY;
	static const char CHAR_IDLE;
	static const char CHAR_EXIT;

	SP_ProcLFServer( const char * bindIP, int port,
			SP_ProcInetServiceFactory * factory );
	~SP_ProcLFServer();

	void setArgs( const SP_ProcArgs_t * args );
	void getArgs( SP_ProcArgs_t * args ) const;

	void setAcceptLock( SP_ProcLock * lock );

	int start();

	int isStop();

	void shutdown();

private:
	char mBindIP[ 64 ];
	int mPort;

	SP_ProcInetServiceFactory * mFactory;
	SP_ProcLock * mLock;

	int mIsStop;
	SP_ProcArgs_t * mArgs;
};

#endif

