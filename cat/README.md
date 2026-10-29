# cat — concatenate and print files (POSIX.1-2024)

An implementation of the `cat` utility of POSIX.1-2024 (XCU cat), in C with
no dependencies beyond libc.

    cat [-u] [file...]

## Build and test

    make           # builds ./cat
    make test      # the tests (sh test_cat.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Input is copied with read() and write(), so whatever one read() returns is
  written at once: output is never delayed, which is what -u asks for.
* `-` is the standard input, each time it appears.
* A file that cannot be read is reported and the others are still copied;
  the exit status is then nonzero. So is a write error.

## Choices where the standard leaves room

* -u is accepted and changes nothing, since the output is unbuffered anyway.

## Testing

`test_cat.sh` runs the utility on prepared input and compares the output and
the exit status: files in order, binary data, `-` (also twice), -u writing
without delay, missing files, `--`, write errors. The binary tested is
`./cat`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `cat.c` | the utility |
| `test_cat.sh` | the tests: `make test`, or `sh test_cat.sh` |
| `Makefile` | build, test, install, clean |
