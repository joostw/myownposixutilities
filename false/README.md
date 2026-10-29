# false — return false value (POSIX.1-2024)

An implementation of the `false` utility of POSIX.1-2024 (XCU false), in C
with no dependencies beyond libc.

    false

## Build and test

    make           # builds ./false
    make test      # the tests (sh test_false.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Exits with status 1, whatever its arguments.

## Testing

`test_false.sh` runs the utility on prepared input and compares the output
and the exit status: the exit status, with and without arguments. The binary
tested is `./false`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `false.c` | the utility |
| `test_false.sh` | the tests: `make test`, or `sh test_false.sh` |
| `Makefile` | build, test, install, clean |
