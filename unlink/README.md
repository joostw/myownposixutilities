# unlink — call the unlink() function (POSIX.1-2024)

An implementation of the `unlink` utility of POSIX.1-2024 (XCU unlink), in C
with no dependencies beyond libc. It belongs to the XSI option.

    unlink file

## Build and test

    make           # builds ./unlink
    make test      # the tests (sh test_unlink.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Calls unlink() on its one operand; a first `--` is discarded.

## Testing

`test_unlink.sh` runs the utility on prepared input and compares the output
and the exit status: removing a file, directories (refused), missing files,
`--`, operand errors. The binary tested is `./unlink`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `unlink.c` | the utility |
| `test_unlink.sh` | the tests: `make test`, or `sh test_unlink.sh` |
| `Makefile` | build, test, install, clean |
