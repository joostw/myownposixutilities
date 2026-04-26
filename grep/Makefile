CC      = cc
CFLAGS  = -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic

.PHONY: all clean

all: grep

grep: grep.o posix_re.o
	$(CC) $(CFLAGS) -o $@ $^

grep.o: grep.c posix_re.h
	$(CC) $(CFLAGS) -c $<

posix_re.o: posix_re.c posix_re.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f grep grep.o posix_re.o
