# crontab - schedule periodic background work (with the daemon crond) (POSIX.1-2024)
#
# Targets: all (the default), test, difftest, install, clean.
#
# crond, the daemon that runs the entries, is installed in $(SBINDIR).

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

PROG     = crontab
OBJS     = crontab.o cron.o
CROND_OBJS = crond.o cron.o

all: $(PROG) crond

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

crond: $(CROND_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(CROND_OBJS) $(LDLIBS)

crontab.o crond.o cron.o: cron.h

.c.o:
	$(CC) $(STDFLAGS) $(CFLAGS) -c $<

test: all
	sh ./test_crontab.sh

difftest:
	@echo "crontab: no differential tests"

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)
	mkdir -p $(DESTDIR)$(SBINDIR)
	cp crond $(DESTDIR)$(SBINDIR)/crond

clean:
	rm -f $(PROG) $(OBJS) crond crond.o

.PHONY: all test difftest install clean
