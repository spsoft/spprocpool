/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <sys/types.h>
#include <sys/socket.h>		/* struct msghdr */
#include <sys/uio.h>			/* struct iovec */
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <fcntl.h>

#include "spprocpdu.hpp"

SP_ProcDataBlock :: SP_ProcDataBlock()
{
	mData = NULL;
	mDataSize = 0;
}

SP_ProcDataBlock :: ~SP_ProcDataBlock()
{
	reset();
}

void * SP_ProcDataBlock :: getData() const
{
	return mData;
}

size_t SP_ProcDataBlock :: getDataSize() const
{
	return mDataSize;
}

void SP_ProcDataBlock :: setData( void * data, size_t dataSize )
{
	reset();

	mData = data;
	mDataSize = dataSize;
}

void SP_ProcDataBlock :: reset()
{
	if( NULL != mData ) free( mData );
	mData = NULL;

	mDataSize = 0;
}

//-------------------------------------------------------------------

SP_ProcClock :: SP_ProcClock()
{
	gettimeofday ( &mBornTime, NULL ); 
	gettimeofday ( &mPrevTime, NULL ); 
}

SP_ProcClock :: ~SP_ProcClock()
{
}

long SP_ProcClock :: getAge()
{
	struct timeval now;
	gettimeofday ( &now, NULL ); 

	return (long)( ( 1000000.0 * ( now.tv_sec - mBornTime.tv_sec )
			+ ( now.tv_usec - mBornTime.tv_usec ) ) / 1000.0 );
}

long SP_ProcClock :: getInterval()
{
	struct timeval now;
	gettimeofday ( &now, NULL ); 

	long ret = long( ( 1000000.0 * ( now.tv_sec - mPrevTime.tv_sec )
			+ ( now.tv_usec - mPrevTime.tv_usec ) ) / 1000.0 );

	mPrevTime = now;

	return ret;
}

//-------------------------------------------------------------------

#define CONTROLLEN sizeof (struct cmsghdr) + sizeof (int)

int SP_ProcPduUtils :: recv_fd( int sockfd )
{
	char tmpbuf[CONTROLLEN];
	struct cmsghdr *cmptr = (struct cmsghdr *) tmpbuf;
	struct iovec iov[1];
	struct msghdr msg;
	char buf[1];

	iov[0].iov_base = buf;
	iov[0].iov_len = sizeof (buf);
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;

	msg.msg_control = cmptr;
	msg.msg_controllen = CONTROLLEN;

	if (recvmsg(sockfd, &msg, 0) <= 0)
		return -1;

	return *(int *) CMSG_DATA (cmptr);
}

int SP_ProcPduUtils :: send_fd (int sockfd, int fd)
{
	char tmpbuf[CONTROLLEN];
	struct cmsghdr *cmptr = (struct cmsghdr *) tmpbuf;
	struct iovec iov[1];
	struct msghdr msg;
	char buf[1];

	iov[0].iov_base = buf;
	iov[0].iov_len = 1;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = cmptr;
	msg.msg_controllen = CONTROLLEN;

	cmptr->cmsg_level = SOL_SOCKET;
	cmptr->cmsg_type = SCM_RIGHTS;
	cmptr->cmsg_len = CONTROLLEN;
	*(int *)CMSG_DATA (cmptr) = fd;

	if (sendmsg(sockfd, &msg, 0) != 1)
		return -1;

	return 0;
}

/* Read "n" bytes from a descriptor. */
ssize_t SP_ProcPduUtils :: readn(int fd, void *vptr, size_t n)
{
	size_t	nleft, nread;
	char	*ptr;

	ptr = (char*)vptr;
	nleft = n;
	while (nleft > 0) {
		if ( (nread = read(fd, ptr, nleft)) < 0)
			return(nread);		/* error, return < 0 */
		else if (nread == 0)
			break;				/* EOF */

		nleft -= nread;
		ptr   += nread;
	}
	return(n - nleft);		/* return >= 0 */
}

/* Write "n" bytes to a descriptor. */
ssize_t SP_ProcPduUtils :: writen(int fd, const void *vptr, size_t n)
{
	size_t		nleft, nwritten;
	const char	*ptr;

	ptr = (char*)vptr;	/* can't do pointer arithmetic on void* */
	nleft = n;
	while (nleft > 0) {
		if ( (nwritten = write(fd, ptr, nleft)) <= 0)
			return(nwritten);		/* error */

		nleft -= nwritten;
		ptr   += nwritten;
	}
	return(n);
}

int SP_ProcPduUtils :: read_pdu( int fd, SP_ProcPdu_t * pdu, SP_ProcDataBlock * block )
{
	int ret = -1;

	memset( pdu, 0, sizeof( SP_ProcPdu_t ) );

	ret = readn( fd, pdu, sizeof( SP_ProcPdu_t ) );
	if( sizeof( SP_ProcPdu_t ) == ret ) {
		if( SP_ProcPdu_t::MAGIC_NUM == pdu->mMagicNum ) { //&& getpid() == pdu->mDestPid ) {
			if( pdu->mDataSize > 0 ) {
				char * buff = (char*)malloc( pdu->mDataSize + 1 );
				assert( NULL != buff );

				ret = readn( fd, buff, pdu->mDataSize );
				if( (int)pdu->mDataSize == ret ) {
					ret = sizeof( SP_ProcPdu_t ) + pdu->mDataSize;
					buff[ pdu->mDataSize ] = '\0';

					block->setData( buff, pdu->mDataSize );
				} else {
					free( buff );
					if( ret < 0 ) {
						syslog( LOG_WARNING, "WARN: read data fail, errno %d, %s",
								errno, strerror( errno ) );
					}
				}
			} else {
				ret = sizeof( SP_ProcPdu_t );
			}
		} else {
			syslog( LOG_WARNING, "WARN: invalid pdu, magic.num %x, dest.pid %d",
					pdu->mMagicNum, pdu->mDestPid );
		}
	} else {
		if( ret < 0 ) {
			syslog( LOG_WARNING, "WARN: read pdu fail, errno %d, %s",
					errno, strerror( errno ) );
		}
	}

	return ret;
}

int SP_ProcPduUtils :: send_pdu( int fd, const SP_ProcPdu_t * pdu, const void * data )
{
	int ret = -1;

	if( sizeof( SP_ProcPdu_t ) == writen( fd, pdu, sizeof( SP_ProcPdu_t ) ) )  {
		if( pdu->mDataSize > 0 ) {
			if( (int)pdu->mDataSize == writen( fd, data, pdu->mDataSize ) ) {
				ret = sizeof( SP_ProcPdu_t ) + pdu->mDataSize;
			} else {
				syslog( LOG_WARNING, "WARN: send data fail, errno %d, %s",
						errno, strerror( errno ) );
			}
		} else {
			ret = sizeof( SP_ProcPdu_t );
		}
	} else {
		syslog( LOG_WARNING, "WARN: send pdu fail, errno %d, %s",
				errno, strerror( errno ) );
	}

	return ret;
}

int SP_ProcPduUtils :: tcp_listen( const char * ip, int port, int * fd )
{
	int ret = 0;

	int listenFd = socket( AF_INET, SOCK_STREAM, 0 );
	if( listenFd < 0 ) {
		syslog( LOG_WARNING, "listen failed, errno %d, %s", errno, strerror( errno ) );
		ret = -1;
	}

	if( 0 == ret ) {
		int flags = 1;
		if( setsockopt( listenFd, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof( flags ) ) < 0 ) {
			syslog( LOG_WARNING, "failed to set setsock to reuseaddr" );
			ret = -1;
		}
		if( setsockopt( listenFd, IPPROTO_TCP, TCP_NODELAY, &flags, sizeof(flags) ) < 0 ) {
			syslog( LOG_WARNING, "failed to set socket to nodelay" );
			ret = -1;
		}
	}

	struct sockaddr_in addr;

	if( 0 == ret ) {
		memset( &addr, 0, sizeof( addr ) );
		addr.sin_family = AF_INET;
		addr.sin_port = htons( port );

		addr.sin_addr.s_addr = INADDR_ANY;
		if( '\0' != *ip ) {
			if( 0 != inet_aton( ip, &addr.sin_addr ) ) {
				syslog( LOG_WARNING, "failed to convert %s to inet_addr", ip );
				ret = -1;
			}
		}
	}

	if( 0 == ret ) {
		if( bind( listenFd, (struct sockaddr*)&addr, sizeof( addr ) ) < 0 ) {
			syslog( LOG_WARNING, "bind failed, errno %d, %s", errno, strerror( errno ) );
			ret = -1;
		}
	}

	if( 0 == ret ) {
		if( ::listen( listenFd, 5 ) < 0 ) {
			syslog( LOG_WARNING, "listen failed, errno %d, %s", errno, strerror( errno ) );
			ret = -1;
		}
	}

	if( 0 != ret && listenFd >= 0 ) close( listenFd );

	if( 0 == ret ) {
		* fd = listenFd;
		syslog( LOG_NOTICE, "Listen on port [%d]", port );
	}

	return ret;
}

void SP_ProcPduUtils :: print_cpu_time()
{
	double user, sys;
	struct rusage myusage, childusage;

	if (getrusage(RUSAGE_SELF, &myusage) < 0)
		perror("getrusage error");
	if (getrusage(RUSAGE_CHILDREN, &childusage) < 0)
		perror("getrusage error");

	user = (double) myusage.ru_utime.tv_sec +
			myusage.ru_utime.tv_usec/1000000.0;
	user += (double) childusage.ru_utime.tv_sec +
			childusage.ru_utime.tv_usec/1000000.0;
	sys = (double) myusage.ru_stime.tv_sec +
			myusage.ru_stime.tv_usec/1000000.0;
	sys += (double) childusage.ru_stime.tv_sec +
			childusage.ru_stime.tv_usec/1000000.0;

	printf("\nuser time = %g, sys time = %g\n", user, sys);
}

