CC     = cc
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic

.PHONY: all clean

all: head

head: head.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f head
