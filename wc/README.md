# wc — word, line, and byte or character count (POSIX.1-2024)

An implementation of the `wc` utility of POSIX.1-2024 (XCU wc), in C with no
dependencies beyond libc.

    wc [-c|-m] [-lw] [file...]

## Build and test

    make           # builds ./wc
    make test      # the tests (sh test_wc.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Lines, words, bytes (-c) or characters of the locale (-m), in the order of
  the standard, with a total line for several files.
* Words are separated by white space of the locale.
* `-` names the standard input.
* Read and write errors make the exit status nonzero.

## Choices where the standard leaves room

* A byte that is not a valid character is not counted as a character, but is
  part of a word.

## Testing

`test_wc.sh` runs the utility on prepared input and compares the output and
the exit status: each count and their combinations, totals, `-`, UTF-8 and
invalid bytes, files that cannot be read, write errors. The binary tested is
`./wc`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `wc.c` | the utility |
| `test_wc.sh` | the tests: `make test`, or `sh test_wc.sh` |
| `Makefile` | build, test, install, clean |
