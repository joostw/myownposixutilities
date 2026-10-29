# tty — return user's terminal name (POSIX.1-2024)

An implementation of the `tty` utility of POSIX.1-2024 (XCU tty), in C with
no dependencies beyond libc.

    tty

## Build and test

    make           # builds ./tty
    make test      # the tests (sh test_tty.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The name ttyname() gives for the standard input, or "not a tty" and status
  1.

## Choices where the standard leaves room

* A terminal whose name cannot be found is an error (status 2).
* A first "--" is discarded.

## Testing

`test_tty.sh` runs the utility on prepared input and compares the output and
the exit status: files, pipes and /dev/null; a pseudo-terminal on the
standard input (and on the output only), with the name the system's tty
finds and the device it names; operands, write errors. The terminal is made
by `testlib/ptyrun.py`. The binary tested is `./tty`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `tty.c` | the utility |
| `test_tty.sh` | the tests: `make test`, or `sh test_tty.sh` |
| `Makefile` | build, test, install, clean |
