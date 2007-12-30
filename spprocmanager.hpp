/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#ifndef __spprocmanager_hpp__
#define __spprocmanager_hpp__

#include <sys/types.h>

class SP_ProcInfo;
class SP_ProcPool;

class SP_ProcWorker {
public:
	virtual ~SP_ProcWorker();

	virtual void process( SP_ProcInfo * procInfo ) = 0;
};

class SP_ProcWorkerFactory {
public:
	virtual ~SP_ProcWorkerFactory();

	virtual SP_ProcWorker * create() const = 0;
};

class SP_ProcManager {
public:
	SP_ProcManager( SP_ProcWorkerFactory * factory );
	~SP_ProcManager();

	void start();

	SP_ProcPool * getProcPool();

private:
	SP_ProcPool * mPool;
	SP_ProcWorkerFactory * mFactory;

	static void sigchild( int signo );
};

#endif

