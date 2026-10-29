# uudecode — decode a binary file (POSIX.1-2024)

An implementation of the `uudecode` utility of POSIX.1-2024 (XCU uudecode),
in C with no dependencies beyond libc.

    uudecode [-o outfile] [file]

## Build and test

    make           # builds ./uudecode
    make test      # the tests (sh test_uudecode.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Both encodings, found from the header; the mode in octal or in the
  symbolic form of chmod, set with fchmod(); the decode pathname, or -o
  outfile; /dev/stdout and "-" for the standard output.

## Choices where the standard leaves room

* A symbolic mode without who letters applies to all, starting from 0.
* An existing output file that cannot be written is an error; the data is
  not written elsewhere.
* Lines after the header that are not valid, and a missing end line, are
  errors (status 1); usage errors have status 2.

## Testing

`test_uudecode.sh` runs the utility on prepared input and compares the
output and the exit status: both encodings of data from Python, round trips
with uuencode, the modes, -o, the standard output, damaged input, errors.
The binary tested is `./uudecode`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `uudecode.c` | the utility |
| `test_uudecode.sh` | the tests: `make test`, or `sh test_uudecode.sh` |
| `Makefile` | build, test, install, clean |
