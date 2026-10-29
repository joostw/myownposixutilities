# vi - screen-oriented (visual) display editor, with ex (POSIX.1-2024)
#
# Targets: all (the default), test, difftest, install, clean.
#
# One binary, ex; vi and view are links to it (it acts by its name).

.POSIX:

CC       = cc
CFLAGS   = -O2
LDFLAGS  =
PREFIX   = /usr/local
BINDIR   = $(PREFIX)/bin

# required by the sources: the language standard; and the warnings
STDFLAGS = -std=c11 -Wall -Wextra
LDLIBS   =

PROG     = ex
OBJS     = main.o util.o buf.o regex.o options.o excmd.o cmds.o exfile.o \
	   term.o screen.o vi.o vinput.o

all: $(PROG) vi view

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

vi: $(PROG)
	rm -f vi
	ln -s $(PROG) vi

view: $(PROG)
	rm -f view
	ln -s $(PROG) view

$(OBJS): ex.h cmd.h vi.h

.c.o:
	$(CC) $(STDFLAGS) $(CFLAGS) -c $<

test: all
	sh ./test_vi.sh

# compares with other implementations; needs
#   nvi and vim (and python3 pyte for the vi cases)
difftest: all
	python3 tests/exdiff.py
	python3 tests/vidiff.py
	python3 tests/rediff.py

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)
	rm -f $(DESTDIR)$(BINDIR)/vi
	ln -s $(PROG) $(DESTDIR)$(BINDIR)/vi
	rm -f $(DESTDIR)$(BINDIR)/view
	ln -s $(PROG) $(DESTDIR)$(BINDIR)/view

clean:
	rm -f $(PROG) $(OBJS) vi view

.PHONY: all test difftest install clean
