/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#ifndef __spprocinetsvr_hpp__
#define __spprocinetsvr_hpp__

#include "spprocserver.hpp"

class SP_ProcInetServer : public SP_ProcBaseServer {
public:
	SP_ProcInetServer( const char * bindIP, int port,
			SP_ProcInetServiceFactory * factory );
	virtual ~SP_ProcInetServer();

	virtual int start();
};

#endif

