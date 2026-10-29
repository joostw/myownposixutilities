# true — return true value (POSIX.1-2024)

An implementation of the `true` utility of POSIX.1-2024 (XCU true), in C
with no dependencies beyond libc.

    true

## Build and test

    make           # builds ./true
    make test      # the tests (sh test_true.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Exits with status 0, whatever its arguments.

## Testing

`test_true.sh` runs the utility on prepared input and compares the output
and the exit status: the exit status, with and without arguments. The binary
tested is `./true`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `true.c` | the utility |
| `test_true.sh` | the tests: `make test`, or `sh test_true.sh` |
| `Makefile` | build, test, install, clean |
