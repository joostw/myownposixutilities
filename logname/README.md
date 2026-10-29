# logname — return the user's login name (POSIX.1-2024)

An implementation of the `logname` utility of POSIX.1-2024 (XCU logname), in
C with no dependencies beyond libc.

    logname

## Build and test

    make           # builds ./logname
    make test      # the tests (sh test_logname.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The name getlogin() gives; when there is none, a diagnostic and status 1.

## Choices where the standard leaves room

* A first "--" is discarded.

## Testing

`test_logname.sh` runs the utility on prepared input and compares the output
and the exit status: the same result as the system's logname (where the
login name is unknown, as in a container, both fail), not taken from
LOGNAME, operands and options. The binary tested is `./logname`, or `$U` if
set.

## Files

| file | contents |
|------|----------|
| `logname.c` | the utility |
| `test_logname.sh` | the tests: `make test`, or `sh test_logname.sh` |
| `Makefile` | build, test, install, clean |
