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
    message_services.o

all: pecontrol

clean:
	rm -f *.o
	rm -f pecontrol


pecontrol: $(OBJS)
	$(CPP) $(OFLAG)pecontrol $(OBJS) $(LDFLAGS)

