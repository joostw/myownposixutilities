# compress - compress and expand data (with uncompress and zcat) (POSIX.1-2024)
#
# Targets: all (the default), test, difftest, install, clean.
#
# uncompress and zcat are links to compress (it acts by its name).

.POSIX:

CC       = cc
CFLAGS   = -O2
LDFLAGS  =
PREFIX   = /usr/local
BINDIR   = $(PREFIX)/bin

# required by the sources: the language standard; and the warnings
STDFLAGS = -std=c11 -Wall -Wextra
LDLIBS   =

PROG     = compress
OBJS     = compress.o lzw.o gzip.o

all: $(PROG) uncompress zcat

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

uncompress: $(PROG)
	rm -f uncompress
	ln -s $(PROG) uncompress

zcat: $(PROG)
	rm -f zcat
	ln -s $(PROG) zcat

$(OBJS): compress.h

.c.o:
	$(CC) $(STDFLAGS) $(CFLAGS) -c $<

test: all
	sh ./test_compress.sh

# compares with other implementations; needs
#   python3 and gzip
difftest: all
	python3 tests/compressdiff.py ./compress

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)
	rm -f $(DESTDIR)$(BINDIR)/uncompress
	ln -s $(PROG) $(DESTDIR)$(BINDIR)/uncompress
	rm -f $(DESTDIR)$(BINDIR)/zcat
	ln -s $(PROG) $(DESTDIR)$(BINDIR)/zcat

clean:
	rm -f $(PROG) $(OBJS) uncompress zcat

.PHONY: all test difftest install clean
