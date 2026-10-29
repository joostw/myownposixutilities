# at - execute commands at a later time (with batch, and the daemon atd) (POSIX.1-2024)
#
# Targets: all (the default), test, difftest, install, clean.
#
# batch is a link to at (it acts by its name).  atd, the daemon that runs
# the jobs, is installed in $(SBINDIR).

.POSIX:

CC       = cc
CFLAGS   = -O2
LDFLAGS  =
PREFIX   = /usr/local
BINDIR   = $(PREFIX)/bin
SBINDIR  = $(PREFIX)/sbin

# required by the sources: the language standard; and the warnings
STDFLAGS = -std=c11 -Wall -Wextra
LDLIBS   =

PROG     = at
OBJS     = at.o spool.o
ATD_OBJS = atd.o spool.o

all: $(PROG) batch atd

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

atd: $(ATD_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(ATD_OBJS) $(LDLIBS)

batch: $(PROG)
	rm -f batch
	ln -s $(PROG) batch

at.o atd.o spool.o: spool.h

.c.o:
	$(CC) $(STDFLAGS) $(CFLAGS) -c $<

test: all
	sh ./test_at.sh

difftest:
	@echo "at: no differential tests"

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)
	rm -f $(DESTDIR)$(BINDIR)/batch
	ln -s $(PROG) $(DESTDIR)$(BINDIR)/batch
	mkdir -p $(DESTDIR)$(SBINDIR)
	cp atd $(DESTDIR)$(SBINDIR)/atd

clean:
	rm -f $(PROG) $(OBJS) batch atd atd.o

.PHONY: all test difftest install clean
