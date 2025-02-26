#
# simple Gnu makefile
#

CPPFLAGS = -g
CPP = g++
OFLAG = -o
LDFLAGS = 
VPATH=.:../Common

.SUFFIXES : .o .cpp .c
.cpp.o :
	$(CPP) $(CPPFLAGS) -c $<
.c.o :
	$(CPP) $(CPPFLAGS) -c $<

OBJS = \
    main.o \
    client_utils.o \
    message_services.o

all: peconsole

clean:
	rm -f *.o
	rm -f peconsole


peconsole: $(OBJS)
	$(CPP) $(OFLAG)peconsole $(OBJS) $(LDFLAGS)

