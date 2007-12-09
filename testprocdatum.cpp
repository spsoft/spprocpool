/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>
#include <assert.h>

#include "spprocdatum.hpp"
#include "spprocpdu.hpp"

class SP_ProcEchoService : public SP_ProcDatumService {
public:
	SP_ProcEchoService(){}

	virtual ~SP_ProcEchoService(){}

	virtual void handle( const SP_ProcDataBlock * request, SP_ProcDataBlock * reply ) {
		char buff[ 512 ] = { 0 };
		snprintf( buff, sizeof( buff ), "worker #%d - %s", getpid(), (char*)request->getData() );
		reply->setData( strdup( buff ), strlen( buff ) );
	}
};

class SP_ProcEchoServiceFactory : public SP_ProcDatumServiceFactory {
public:
	SP_ProcEchoServiceFactory() {}
	virtual ~SP_ProcEchoServiceFactory() {}

	virtual SP_ProcDatumService * create() const {
		return new SP_ProcEchoService();
	}
};

class SP_ProcEchoHandler : public SP_ProcDatumHandler {
public:
	SP_ProcEchoHandler() {}
	virtual ~SP_ProcEchoHandler() {}

	virtual void onReply( pid_t pid, const SP_ProcDataBlock * reply ) {
		assert( NULL != reply->getData() );
		printf( "reply: %s\n", (char*)reply->getData() );
	}

	virtual void onError( pid_t pid ) {
		printf( "erro: process %d\n", pid );
	}
};

int main( int argc, char * argv[] )
{
#ifdef LOG_PERROR
	openlog( "testprocdatum", LOG_CONS | LOG_PID | LOG_PERROR, LOG_USER );
#else
	openlog( "testprocdatum", LOG_CONS | LOG_PID, LOG_USER );
#endif

	SP_ProcDatumDispatcher dispatcher( new SP_ProcEchoServiceFactory(),
			new SP_ProcEchoHandler() );

	dispatcher.setMaxIdleTimeout( 5 );
	dispatcher.setMaxRequestsPerProc( 2 );

	char buff[ 256 ] = { 0 };
	for( int i = 0; i < 10; i++ ) {
		snprintf( buff, sizeof( buff ), "Index %d, Hello world!", i );
		pid_t pid = dispatcher.dispatch( buff, strlen( buff ) );
		printf( "dispatch %d to worker #%d\n", i, pid );

		if( 0 == ( i % 3 ) ) sleep( 1 );
	}

	printf( "wait 5 seconds for all processes to complete\n" );

	sleep( 10 );

	dispatcher.dump();

	closelog();

	return 0;
}

