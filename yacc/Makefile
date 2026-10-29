# yacc - yet another compiler compiler (POSIX.1-2024)
#
# Targets: all (the default), test, difftest, install, clean.

.POSIX:

CC       = cc
CFLAGS   = -O2
LDFLAGS  =
PREFIX   = /usr/local
BINDIR   = $(PREFIX)/bin
LIBDIR   = $(PREFIX)/lib

# required by the sources: the language standard; and the warnings
STDFLAGS = -std=c11 -Wall -Wextra
LDLIBS   =

PROG     = yacc
OBJS     = main.o reader.o lalr.o tables.o output.o verbose.o util.o
LIB      = liby.a
LIBOBJS  = liby/main.o liby/yyerror.o

all: $(PROG) $(LIB)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(LIB): $(LIBOBJS)
	rm -f $@
	ar -rc $@ $(LIBOBJS)

liby/main.o: liby/main.c
	$(CC) $(CFLAGS) -c -o liby/main.o liby/main.c

liby/yyerror.o: liby/yyerror.c
	$(CC) $(CFLAGS) -c -o liby/yyerror.o liby/yyerror.c

$(OBJS): yacc.h

.c.o:
	$(CC) $(STDFLAGS) $(CFLAGS) -c $<

test: all
	sh ./test_yacc.sh

# compares with other implementations; needs
#   bison and byacc
difftest: all
	python3 tests/specdiff.py ./yacc 300
	python3 tests/randgram.py ./yacc 1 100

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)
	mkdir -p $(DESTDIR)$(LIBDIR)
	cp $(LIB) $(DESTDIR)$(LIBDIR)/$(LIB)

clean:
	rm -f $(PROG) $(OBJS) $(LIB) $(LIBOBJS)

.PHONY: all test difftest install clean
