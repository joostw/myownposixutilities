# comm — select or reject lines common to two files (POSIX.1-2024)

An implementation of the `comm` utility of POSIX.1-2024 (XCU comm), in C
with no dependencies beyond libc.

    comm [-123] file1 file2

## Build and test

    make           # builds ./comm
    make test      # the tests (sh test_comm.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The three columns and their leading tabs; lines compared with strcoll()
  and then byte by byte, as the standard asks; lines of any length.

## Choices where the standard leaves room

* A last line without a newline is written with one.

## Testing

`test_comm.sh` runs the utility on prepared input and compares the output
and the exit status: every combination of -1, -2 and -3 (the same output as
GNU comm), empty files, standard input, duplicate lines, missing newlines,
errors. The binary tested is `./comm`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `comm.c` | the utility |
| `test_comm.sh` | the tests: `make test`, or `sh test_comm.sh` |
| `Makefile` | build, test, install, clean |
