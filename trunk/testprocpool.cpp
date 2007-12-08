/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <syslog.h>

#include "spprocmanager.hpp"
#include "spprocpool.hpp"

//-------------------------------------------------------------------
class SP_EchoWorker : public SP_ProcWorker {
public:
	SP_EchoWorker(){}
	virtual ~SP_EchoWorker(){}

	virtual void process( const SP_ProcInfo * procInfo ) {
		for( ; ; ) {
			char buff[ 256 ] = { 0 };
			int len = read( procInfo->getPipeFd(), buff, sizeof( buff ) );
			if( len > 0 ) {
				char newBuff[ 256 ] = { 0 };
				snprintf( newBuff, sizeof( newBuff ), "<%d> %s", getpid(), buff );
				write( procInfo->getPipeFd(), newBuff, strlen( newBuff ) );
			} else {
				break;
			}
		}
	}
};

class SP_EchoWorkerFactory : public SP_ProcWorkerFactory {
public:
	SP_EchoWorkerFactory(){}
	virtual ~SP_EchoWorkerFactory(){}

	virtual SP_ProcWorker * create() const {
		return new SP_EchoWorker();
	}
};

//-------------------------------------------------------------------

void * echoClient( void * args )
{
	const char * text = "Hello, world!";

	SP_ProcPool * procPool = (SP_ProcPool*)args;

	SP_ProcInfo * info = procPool->get();

	if( write( info->getPipeFd(), text, strlen( text ) ) > 0 ) {
		char buff[ 256 ] = { 0 };
		memset( buff, 0, sizeof( buff ) );
		if( read( info->getPipeFd(), buff, sizeof( buff ) - 1 ) ) {
			printf( "read: %d - %s\n", info->getPid(), buff );
		}
	}

	procPool->save( info );

	return NULL;
}

int main( int argc, char * argv[] )
{
#ifdef LOG_PERROR
	openlog( "testprocpool", LOG_CONS | LOG_PID | LOG_PERROR, LOG_USER );
#else
	openlog( "testprocpool", LOG_CONS | LOG_PID, LOG_USER );
#endif

	SP_ProcManager manager( new SP_EchoWorkerFactory() );
	manager.start();

	SP_ProcPool * procPool = manager.getProcPool();

	static int MAX_TEST_THREAD = 10;

	pthread_t threadArray[ MAX_TEST_THREAD ];

	for( int i = 0; i < MAX_TEST_THREAD; i++ ) {
		pthread_create( &( threadArray[i] ), NULL, echoClient, procPool );
	}

	for( int i = 0; i < MAX_TEST_THREAD; i++ ) {
		pthread_join( threadArray[i], NULL );
	}

	procPool->dump();

	closelog();

	return 0;
}

