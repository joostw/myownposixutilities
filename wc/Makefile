CC     = cc
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic

.PHONY: all clean

all: wc

wc: wc.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f wc
