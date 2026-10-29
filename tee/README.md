# tee — duplicate standard input (POSIX.1-2024)

An implementation of the `tee` utility of POSIX.1-2024 (XCU tee), in C with
no dependencies beyond libc.

    tee [-ai] [file...]

## Build and test

    make           # builds ./tee
    make test      # the tests (sh test_tee.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Copies the standard input to the standard output and to each file; -a
  appends, -i ignores SIGINT.
* A file that cannot be opened is reported, and the others are still
  written; the exit status is then nonzero.

## Testing

`test_tee.sh` runs the utility on prepared input and compares the output and
the exit status: copying to standard output and files (13 at once), -a, -i,
`-` as a file name, files that cannot be opened, write errors. The binary
tested is `./tee`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `tee.c` | the utility |
| `test_tee.sh` | the tests: `make test`, or `sh test_tee.sh` |
| `Makefile` | build, test, install, clean |
