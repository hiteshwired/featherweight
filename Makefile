# New simplified Makefile. Edit SHAREDLIBS line to use Dr. Nico's library
CC      = gcc
CFLAGS  = -Wall -fPIC -g -I. -I../../include
# SHAREDLIBS = util64.o libPLN.so libsnakes.so
SHAREDLIBS = util64.o liblwp.so libsnakes.so

SNAKEOBJS  = randomsnakes.o util.o
SNAKELIBS = -lncurses -lrt


LIBS 	= util64.o liblwp.so

TESTOBJS = lwp64.o rr64.o magic64.o
OBJS 	= $(OBJS64) $(SNAKEOBJS)
OBJS64 = lwp64.o magic64.o rr64.o
SRCS	= lwp.c util.c rr.c tid.c magic.s
HDRS	= 

all: 	$(LIBS)

clean:	
	rm -f $(OBJS) *~ TAGS *.a *.o nums hungry snakes lwptest lwpedge liblwp.so

nums: numbersmain.o $(SHAREDLIBS)
	$(CC) $(CFLAGS) -g -o nums numbersmain.o $(SHAREDLIBS) $(SNAKELIBS)

lwptest: lwptest.o $(TESTOBJS)
	$(CC) $(CFLAGS) -g -o lwptest lwptest.o $(TESTOBJS)

lwptest.o: lwptest.c lwp.h rr.h
	$(CC) $(CFLAGS) -c lwptest.c

test: lwptest
	./lwptest

lwpedge: lwpedge.o $(TESTOBJS)
	$(CC) $(CFLAGS) -g -o lwpedge lwpedge.o $(TESTOBJS)

lwpedge.o: lwpedge.c lwp.h rr.h
	$(CC) $(CFLAGS) -c lwpedge.c

edgetest: lwpedge
	./lwpedge; s=$$?; echo "exit code: $$s"; exit $$s

numbersmain.o: numbersmain.c AlwaysZero.c lwp.h
	$(CC) $(CFLAGS) -c numbersmain.c -L. -llwp

magic.o: magic.s
	$(CC) $(CFLAGS) -c -o magic.o magic.s

%64.o: %.c
	$(CC) $(CFLAGS) -m64 -c -o $@ $*.c

%64.o: %64.S
	$(CC) $(CFLAGS) -m64 -c -o $@ $<

lwp.o: lwp.h

liblwp.so: $(OBJS64)
	$(CC) $(CFLAGS) -shared -o $@ $(OBJS64)

snakes: randomsnakes.o AlwaysZero.c $(SHAREDLIBS)
	$(CC) $(CFLAGS) -o snakes randomsnakes.o $(SHAREDLIBS) $(SNAKELIBS)

hungry: hungrysnakes.o AlwaysZero.c $(SHAREDLIBS)
	$(CC) $(CFLAGS) -o hungry hungrysnakes.o $(SHAREDLIBS) $(SNAKELIBS)

hungrysnakes.o: hungrysnakes.c AlwaysZero.c util.c lwp.h snakes.h
	$(CC) $(CFLAGS) -c hungrysnakes.c -L. -llwp

randomsnakes.o: randomsnakes.c AlwaysZero.c util.c lwp.h snakes.h
	$(CC) $(CFLAGS) -c randomsnakes.c -L. -llwp

numbermain.o: numbersmain.c lwp.h
	$(CC) $(CFLAGS) -c numbersmain.c

rs: snakes
	(export LD_LIBRARY_PATH=.; ./snakes)

hs: hungry
	(export LD_LIBRARY_PATH=.; ./hungry)

ns: nums
	(export LD_LIBRARY_PATH=.; ./nums)

SUBMIT = Makefile lwp.c lwp.h fp.h magic64.S rr.c rr.h util.c

submit: $(SUBMIT)
	tar -czf program2_submission.tgz $(SUBMIT)
