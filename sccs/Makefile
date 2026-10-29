# sccs - the SCCS utilities: admin, delta, get, prs, rmdel, sact, sccs, unget, val, what (POSIX.1-2024)
#
# Targets: all (the default), test, difftest, install, clean.
#
# One program, sccs; the others are links to it (it acts by its name).

.POSIX:

CC       = cc
CFLAGS   = -O2
LDFLAGS  =
PREFIX   = /usr/local
BINDIR   = $(PREFIX)/bin

# required by the sources: the language standard; and the warnings
STDFLAGS = -std=c11 -Wall -Wextra
LDLIBS   =

PROG     = sccs
OBJS     = main.o sfile.o ldiff.o admin.o delta.o get.o prs.o rmdel.o sact.o \
	   unget.o val.o what.o front.o

all: $(PROG) admin delta get prs rmdel sact unget val what

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

admin: $(PROG)
	rm -f admin
	ln -s $(PROG) admin

delta: $(PROG)
	rm -f delta
	ln -s $(PROG) delta

get: $(PROG)
	rm -f get
	ln -s $(PROG) get

prs: $(PROG)
	rm -f prs
	ln -s $(PROG) prs

rmdel: $(PROG)
	rm -f rmdel
	ln -s $(PROG) rmdel

sact: $(PROG)
	rm -f sact
	ln -s $(PROG) sact

unget: $(PROG)
	rm -f unget
	ln -s $(PROG) unget

val: $(PROG)
	rm -f val
	ln -s $(PROG) val

what: $(PROG)
	rm -f what
	ln -s $(PROG) what

$(OBJS): sccs.h

.c.o:
	$(CC) $(STDFLAGS) $(CFLAGS) -c $<

test: all
	sh ./test_sccs.sh

# compares with other implementations; needs
#   python3 and CSSC (its programs and its sccs)
difftest: all
	python3 tests/csscdiff.py . 1 100
	python3 tests/csscdiff.py . 2 100

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)
	rm -f $(DESTDIR)$(BINDIR)/admin
	ln -s $(PROG) $(DESTDIR)$(BINDIR)/admin
	rm -f $(DESTDIR)$(BINDIR)/delta
	ln -s $(PROG) $(DESTDIR)$(BINDIR)/delta
	rm -f $(DESTDIR)$(BINDIR)/get
	ln -s $(PROG) $(DESTDIR)$(BINDIR)/get
	rm -f $(DESTDIR)$(BINDIR)/prs
	ln -s $(PROG) $(DESTDIR)$(BINDIR)/prs
	rm -f $(DESTDIR)$(BINDIR)/rmdel
	ln -s $(PROG) $(DESTDIR)$(BINDIR)/rmdel
	rm -f $(DESTDIR)$(BINDIR)/sact
	ln -s $(PROG) $(DESTDIR)$(BINDIR)/sact
	rm -f $(DESTDIR)$(BINDIR)/unget
	ln -s $(PROG) $(DESTDIR)$(BINDIR)/unget
	rm -f $(DESTDIR)$(BINDIR)/val
	ln -s $(PROG) $(DESTDIR)$(BINDIR)/val
	rm -f $(DESTDIR)$(BINDIR)/what
	ln -s $(PROG) $(DESTDIR)$(BINDIR)/what

clean:
	rm -f $(PROG) $(OBJS) admin delta get prs rmdel sact unget val what

.PHONY: all test difftest install clean
