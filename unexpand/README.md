# unexpand — convert spaces to tabs (POSIX.1-2024)

An implementation of the `unexpand` utility of POSIX.1-2024 (XCU unexpand),
in C with no dependencies beyond libc.

    unexpand [-a|-t tablist] [file...]

## Build and test

    make           # builds ./unexpand
    make test      # the tests (sh test_unexpand.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Runs of blanks are held back and written as the most tabs and the fewest
  spaces that fill the same columns: at the start of a line, and with -a (or
  -t) everywhere; past the last stop of a list nothing is converted.

## Choices where the standard leaves room

* A run of a single space stays a space.
* A backspace ends the leading blanks.

## Testing

`test_unexpand.sh` runs the utility on prepared input and compares the
output and the exit status: leading blanks, -a with runs over one and two
stops, -t lists and past them, single spaces, backspaces, trailing blanks.
Random lines agree with GNU unexpand. The binary tested is `./unexpand`, or
`$U` if set.

## Files

| file | contents |
|------|----------|
| `unexpand.c` | the utility |
| `test_unexpand.sh` | the tests: `make test`, or `sh test_unexpand.sh` |
| `Makefile` | build, test, install, clean |
