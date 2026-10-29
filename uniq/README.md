# uniq — report or filter out repeated lines in a file (POSIX.1-2024)

An implementation of the `uniq` utility of POSIX.1-2024 (XCU uniq), in C
with no dependencies beyond libc.

    uniq [-c|-d|-u] [-f fields] [-s chars] [input_file [output_file]]

## Build and test

    make           # builds ./uniq
    make test      # the tests (sh test_uniq.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* -c, -d, -u, -f and -s as specified; -s counts characters of the locale.
* An input_file or output_file of `-` is the standard input or output.
* A write error makes the exit status nonzero.

## Choices where the standard leaves room

* -d and -u together write nothing.

## Testing

`test_uniq.sh` runs the utility on prepared input and compares the output
and the exit status: each option and their combinations, fields and
characters skipped, UTF-8, input and output files, errors. The binary tested
is `./uniq`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `uniq.c` | the utility |
| `test_uniq.sh` | the tests: `make test`, or `sh test_uniq.sh` |
| `Makefile` | build, test, install, clean |
