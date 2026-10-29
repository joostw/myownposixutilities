# mkdir — make directories (POSIX.1-2024)

An implementation of the `mkdir` utility of POSIX.1-2024 (XCU mkdir), in C
with no dependencies beyond libc.

    mkdir [-p] [-m mode] dir...

## Build and test

    make           # builds ./mkdir
    make test      # the tests (sh test_mkdir.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* -p creates missing parent directories, with the permissions the standard
  specifies (u+wx added to the umask's result).
* -m takes an octal or a symbolic mode, as chmod does: with no *who* the
  bits in the umask are left alone, and `=` with no *who* clears all bits
  first.
* The new directory is never more permissive than the mode, at any moment.

## Testing

`test_mkdir.sh` runs the utility on prepared input and compares the output
and the exit status: modes (octal, symbolic, against umasks), -p and its
intermediate directories, existing directories, errors. The binary tested is
`./mkdir`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `mkdir.c` | the utility |
| `test_mkdir.sh` | the tests: `make test`, or `sh test_mkdir.sh` |
| `Makefile` | build, test, install, clean |
