CC     = cc
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -Wall -Wextra -Wpedantic

.PHONY: all clean

all: mkdir

mkdir: mkdir.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f mkdir
