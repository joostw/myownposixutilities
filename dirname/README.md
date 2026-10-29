# dirname — return the directory portion of a pathname (POSIX.1-2024)

An implementation of the `dirname` utility of POSIX.1-2024 (XCU dirname), in
C with no dependencies beyond libc.

    dirname string

## Build and test

    make           # builds ./dirname
    make test      # the tests (sh test_dirname.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The steps of the standard in order: empty strings, trailing slashes, the
  last component, and the slashes before it.
* A first `--` is discarded (XCU 1.4).
* A write error makes the exit status nonzero.

## Choices where the standard leaves room

* An empty string gives `.`.
* `//` is treated like any other string of slashes.

## Testing

`test_dirname.sh` runs the utility on prepared input and compares the output
and the exit status: the steps one by one, relative and absolute names,
`--`, operand errors, write errors. The binary tested is `./dirname`, or
`$U` if set.

## Files

| file | contents |
|------|----------|
| `dirname.c` | the utility |
| `test_dirname.sh` | the tests: `make test`, or `sh test_dirname.sh` |
| `Makefile` | build, test, install, clean |
