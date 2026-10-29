# rmdir — remove directories (POSIX.1-2024)

An implementation of the `rmdir` utility of POSIX.1-2024 (XCU rmdir), in C
with no dependencies beyond libc.

    rmdir [-p] dir...

## Build and test

    make           # builds ./rmdir
    make test      # the tests (sh test_rmdir.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* rmdir() for each operand, in order; -p then removes the directories named
  by the operand's leading components, as rmdir -p $(dirname dir) would,
  stopping at the first that cannot be removed (reported).

## Choices where the standard leaves room

* -p stops before "." and "/".

## Testing

`test_rmdir.sh` runs the utility on prepared input and compares the output
and the exit status: single directories, missing and non-empty ones, files,
the order of operands, -p with doubled and trailing slashes and with an
absolute path, dot, and (not as root) read-only parents. The binary tested
is `./rmdir`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `rmdir.c` | the utility |
| `test_rmdir.sh` | the tests: `make test`, or `sh test_rmdir.sh` |
| `Makefile` | build, test, install, clean |
