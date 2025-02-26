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
    main.o   \
    parser.o \
    utils.o \
    message_services.o

all: printem

clean:
	rm -f *.o
	rm -f printem


printem: $(OBJS)
	$(CPP) $(OFLAG)printem $(OBJS) $(LDFLAGS)

