
#--------------------------------------------------------------------

CC = gcc
AR = ar cru
CFLAGS = -Wall -D_REENTRANT -D_GNU_SOURCE -g -fPIC
SOFLAGS = -shared
LDFLAGS = -lstdc++ -lpthread -lresolv

LINKER = $(CC)
LINT = lint -c
RM = /bin/rm -f

ifeq ($(origin version), undefined)
	version = 0.1
endif

OS=$(shell uname)

ifeq ($(OS), SunOS)
	LDFLAGS += -lnsl -lsocket
endif

#--------------------------------------------------------------------

LIBOBJS = spprocpdu.o spprocmanager.o spprocpool.o \
		spprocdatum.o spprocinet.o

TARGET =  libspprocpool.so

TEST_TARGET = testprocpdu testprocpool testprocdatum \
		testinetserver testinetclient

#--------------------------------------------------------------------

all: $(TARGET) $(TEST_TARGET)

libspprocpool.so: $(LIBOBJS)
	$(LINKER) $(SOFLAGS) $^ -o $@

test: all $(TEST_TARGET)

testprocpdu: spprocpdu.o testprocpdu.o
	$(LINKER) $(LDFLAGS) $^ -o $@

testprocpool: testprocpool.o
	$(LINKER) $(LDFLAGS) $^ -o $@ -L. -lspprocpool

testprocdatum: testprocdatum.o
	$(LINKER) $(LDFLAGS) $^ -o $@ -L. -lspprocpool

testinetserver: testinetserver.o
	$(LINKER) $(LDFLAGS) $^ -o $@ -L. -lspprocpool

testinetclient: testinetclient.o
	$(LINKER) $(LDFLAGS) $^ -o $@ -L. -lspprocpool

dist: clean spprocpool-$(version).src.tar.gz

spprocpool-$(version).src.tar.gz:
	@ls | grep -v CVS | grep -v "\.so" | sed 's:^:spprocpool-$(version)/:' > MANIFEST
	@(cd ..; ln -s spprocpool spprocpool-$(version))
	(cd ..; tar cvf - `cat spprocpool/MANIFEST` | gzip > spprocpool/spprocpool-$(version).src.tar.gz)
	@(cd ..; rm spprocpool-$(version))

clean:
	@( $(RM) *.o vgcore.* core core.* $(TARGET) $(TEST_TARGET) )

#--------------------------------------------------------------------

# make rule
%.o : %.c
	$(CC) $(CFLAGS) -c $^ -o $@	

%.o : %.cpp
	$(CC) $(CFLAGS) -c $^ -o $@	

