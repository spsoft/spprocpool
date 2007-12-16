/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#ifndef __spprocpdu_hpp__
#define __spprocpdu_hpp__

#include <sys/types.h>

typedef struct tagSP_ProcPdu {
	enum { MAGIC_NUM = 0x20071206 };

	unsigned int mMagicNum;
	pid_t mSrcPid;
	pid_t mDestPid;
	size_t mDataSize;
} SP_ProcPdu_t;

class SP_ProcDataBlock {
public:
	SP_ProcDataBlock();
	~SP_ProcDataBlock();

	void * getData() const;
	size_t getDataSize() const;

	void setData( void * data, size_t dataSize );

	void reset();

private:
	void * mData;
	size_t mDataSize;
};

class SP_ProcClock {
public:
	SP_ProcClock();
	~SP_ProcClock();

	// @return ms
	long getAge();

	// @return ms
	long getInterval();

private:
	struct timeval mBornTime;
	struct timeval mPrevTime;
};

class SP_ProcPduUtils {
public:

	// > 0 : OK, 0 : connect reset by peer, -1 : error
	static int read_pdu( int fd, SP_ProcPdu_t * pdu, SP_ProcDataBlock * block );

	// > 0 : OK, -1 : error
	static int send_pdu( int fd, const SP_ProcPdu_t * pdu, const void * data );

	// >= 0 : OK, -1 : error
	static int tcp_listen( const char * ip, int port, int * fd );

	static void print_cpu_time();

	/*
	 * The following functions are adapted from APUE (by W. Richard Stevens).
	 */

	/* Pass a file descriptor to another process.
	 */
	static int send_fd (int sockfd, int fd);

	/* Receive a file descriptor from another process.
	 */
	static int recv_fd( int sockfd );

	/* Read "n" bytes from a descriptor. */
	static ssize_t readn(int fd, void *vptr, size_t n);

	/* Write "n" bytes to a descriptor. */
	static ssize_t writen(int fd, const void *vptr, size_t n);

private:
	SP_ProcPduUtils();
	~SP_ProcPduUtils();
};

#endif

