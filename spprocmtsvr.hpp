/*
 * Copyright 2007-2008 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#ifndef __spprocmtsvr_hpp__
#define __spprocmtsvr_hpp__

#include "spprocserver.hpp"

class SP_ProcInetServiceFactory;
class SP_ProcManager;
class SP_ProcPool;
class SP_ProcInfoList;
class SP_ProcLock;

typedef struct tagSP_ProcArgs SP_ProcArgs_t;

class SP_ProcMTServer : public SP_ProcBaseServer {
public:
	SP_ProcMTServer( const char * bindIP, int port,
			SP_ProcInetServiceFactory * factory );
	virtual ~SP_ProcMTServer();

	virtual int start();

	// default is 10
	void setThreadsPerProc( int threadsPerProc );

	void setAcceptLock( SP_ProcLock * lock );

private:
	SP_ProcLock * mLock;
	int mThreadsPerProc;
};

#endif

