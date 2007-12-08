/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>

#include "spprocpdu.hpp"

int main( int argc, char * argv[] )
{
	const char * text = "Hello, world!";

	int pipeFd[ 2 ] = { -1, -1 };

	if( socketpair( AF_UNIX, SOCK_STREAM, 0, pipeFd ) < 0 ) {
		perror( "socketpair fail" );
		exit( -1 );
	}

	pid_t pid = fork();
	if( pid < 0 ) {
		perror( "fork fail" );
		exit( -1 );
	}

	if( 0 == pid ) {
		close( pipeFd[1] );

		int fd = open( "/tmp/testprocutils.txt", O_CREAT | O_RDWR | O_TRUNC, 0666 );
		if( fd > 0 ) write( fd, text, strlen( text ) );

		SP_ProcPduUtils::send_fd( pipeFd[0], fd );

		if( fd > 0 ) close( fd );
	} else {
		close( pipeFd[0] );

		int fd = SP_ProcPduUtils::recv_fd( pipeFd[1] );

		if( fd > 0 ) {
			lseek( fd, 0, SEEK_SET );

			char buff[ 256 ] = { 0 };

			memset( buff, 0, sizeof( buff ) );
			read( fd, buff, sizeof( buff ) - 1 );

			printf( "read: %s\n", buff );

			close( fd );
		}
	}

	return 0;
}

