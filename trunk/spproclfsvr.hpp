/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#ifndef __spproclfsvr_hpp__ 
#define __spproclfsvr_hpp__

#include "spprocserver.hpp"

class SP_ProcInetServiceFactory;
class SP_ProcManager;
class SP_ProcPool;
class SP_ProcInfoList;
class SP_ProcLock;

typedef struct tagSP_ProcArgs SP_ProcArgs_t;

class SP_ProcLFServer : public SP_ProcBaseServer {
public:
	SP_ProcLFServer( const char * bindIP, int port,
			SP_ProcInetServiceFactory * factory );
	virtual ~SP_ProcLFServer();

	virtual int start();

	void setAcceptLock( SP_ProcLock * lock );

private:
	SP_ProcLock * mLock;
};

#endif

