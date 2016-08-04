
# Rough makefile for building the Microsoft Windows ltotape backend
#  under cygwin.
#
#  Expect this to change majorly before it becomes useful..

CC=/bin/gcc
CFLAGS=-g -c -DHP_BUILD
HAX=-DUChar="unsigned char"
INC=-I. -I../../..

SRCS=ltotape_platform.c ltotape_diag.c ltotape.c

LIBRARY: $(SRCS)
	$(CC) $(CFLAGS) $(INC) $(HAX) $(SRCS)

clean:
	rm -f $(SRCS:.c=.o) testharness.exe

# Test harness program, purely for trying out the backend functions before LTFS ported..
testharness: $(SRCS) testharness.c
	$(CC) -g $(INC) testharness.c $(SRCS:.c=.o) -o testharness

