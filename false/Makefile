CC     = cc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic

.PHONY: all clean

all: false

false: false.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f false
