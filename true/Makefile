CC     = cc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic

.PHONY: all clean

all: true

true: true.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f true
