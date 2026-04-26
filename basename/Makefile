CC     = cc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic

.PHONY: all clean

all: basename

basename: basename.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f basename
