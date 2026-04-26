CC     = cc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic

.PHONY: all clean

all: dirname

dirname: dirname.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f dirname
