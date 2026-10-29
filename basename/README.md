# basename — return non-directory portion of a pathname (POSIX.1-2024)

An implementation of the `basename` utility of POSIX.1-2024 (XCU basename),
in C with no dependencies beyond libc.

    basename string [suffix]

## Build and test

    make           # builds ./basename
    make test      # the tests (sh test_basename.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The steps of the standard in order: an empty string, a string of slashes,
  trailing slashes, the directory prefix, then the suffix (removed only when
  it is not the whole remaining string).
* A first `--` is discarded, as for every utility with operands but no
  options (XCU 1.4).
* A write error makes the exit status nonzero.

## Choices where the standard leaves room

* An empty string gives `.` (the standard allows `.` or an empty line).
* `//` is treated like any other string of slashes, giving `/`.

## Testing

`test_basename.sh` runs the utility on prepared input and compares the
output and the exit status: the steps one by one, suffixes (matching, whole
string, absent), `--`, missing and extra operands, write errors. The binary
tested is `./basename`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `basename.c` | the utility |
| `test_basename.sh` | the tests: `make test`, or `sh test_basename.sh` |
| `Makefile` | build, test, install, clean |
