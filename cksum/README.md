# cksum — write file checksums and sizes (POSIX.1-2024)

An implementation of the `cksum` utility of POSIX.1-2024 (XCU cksum), in C
with no dependencies beyond libc.

    cksum [file...]

## Build and test

    make           # builds ./cksum
    make test      # the tests (sh test_cksum.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The CRC of the standard (the Ethernet polynomial, the length appended,
  complemented), with a table; sizes of any length. A first "--" is
  discarded.

## Testing

`test_cksum.sh` runs the utility on prepared input and compares the output
and the exit status: known values (the same as GNU cksum), binary data, a
large file, several files, missing files, write errors. The binary tested is
`./cksum`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `cksum.c` | the utility |
| `test_cksum.sh` | the tests: `make test`, or `sh test_cksum.sh` |
| `Makefile` | build, test, install, clean |
