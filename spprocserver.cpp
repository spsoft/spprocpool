/*
 * Copyright 2010 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <stdlib.h>

#include "spprocserver.hpp"

#include "spprocpool.hpp"
#include "spprocmanager.hpp"
#include "spprocpdu.hpp"

SP_ProcInetService :: ~SP_ProcInetService()
{
}

//-------------------------------------------------------------------

SP_ProcInetServiceFactory :: ~SP_ProcInetServiceFactory()
{
}

void SP_ProcInetServiceFactory :: workerInit( const SP_ProcInfo * procInfo )
{
}

void SP_ProcInetServiceFactory :: workerEnd( const SP_ProcInfo * procInfo )
{
}

//-------------------------------------------------------------------

SP_ProcBaseServer :: SP_ProcBaseServer( const char * bindIP, int port,
		SP_ProcInetServiceFactory * factory )
{
	strncpy( mBindIP, bindIP, sizeof( mBindIP ) );
	mBindIP[ sizeof( mBindIP ) - 1 ] = '\0';

	mPort = port;

	mFactory = factory;

	mArgs = (SP_ProcArgs_t*)malloc( sizeof( SP_ProcArgs_t ) );
	mArgs->mMaxProc = 64;
	mArgs->mMaxIdleProc = 5;
	mArgs->mMinIdleProc = 1;

	mMaxRequestsPerProc = 0;

	mIsStop = 1;
}

SP_ProcBaseServer :: ~SP_ProcBaseServer()
{
	free( mArgs );
	mArgs = NULL;
}

void SP_ProcBaseServer :: setArgs( const SP_ProcArgs_t * args )
{
	* mArgs = * args;

	if( mArgs->mMinIdleProc <= 0 ) mArgs->mMinIdleProc = 1;
	if( mArgs->mMaxIdleProc < mArgs->mMinIdleProc ) mArgs->mMaxIdleProc = mArgs->mMinIdleProc;
	if( mArgs->mMaxProc <= 0 ) mArgs->mMaxProc = mArgs->mMaxIdleProc;
}

void SP_ProcBaseServer :: getArgs( SP_ProcArgs_t * args ) const
{
	* args = * mArgs;
}

void SP_ProcBaseServer :: setMaxRequestsPerProc( int maxRequestsPerProc )
{
	mMaxRequestsPerProc = maxRequestsPerProc;
}

void SP_ProcBaseServer :: shutdown()
{
	mIsStop = 1;
}

int SP_ProcBaseServer :: isStop()
{
	return mIsStop;
}

