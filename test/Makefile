# test - evaluate expression (POSIX.1-2024)
#
# Targets: all (the default), test, difftest, install, clean.
#
# [ is a link to test (it acts by its name).  As "test" is the target
# that runs the tests, the program is made with the file prog.made, which
# records when it was linked, and [ by the target lbracket.

.POSIX:

CC       = cc
CFLAGS   = -O2
LDFLAGS  =
PREFIX   = /usr/local
BINDIR   = $(PREFIX)/bin

# required by the sources: the language standard; and the warnings
STDFLAGS = -std=c11 -Wall -Wextra
LDLIBS   =

PROG     = test
OBJS     = test.o

all: prog.made lbracket

prog.made: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROG) $(OBJS) $(LDLIBS)
	: > prog.made

lbracket: prog.made
	rm -f '['
	ln -s $(PROG) '['

.c.o:
	$(CC) $(STDFLAGS) $(CFLAGS) -c $<

test: all
	sh ./test_test.sh

difftest:
	@echo "test: no differential tests"

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)
	rm -f $(DESTDIR)$(BINDIR)/'['
	ln -s $(PROG) $(DESTDIR)$(BINDIR)/'['

clean:
	rm -f $(PROG) $(OBJS) '[' prog.made

.PHONY: all test difftest install clean lbracket
