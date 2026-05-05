CC     = cc
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic

.PHONY: all clean

all: sed

sed: sed.o
	$(CC) $(CFLAGS) -o $@ $^

sed.o: sed.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f sed sed.o
