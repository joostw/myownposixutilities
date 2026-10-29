# link — call the link() function (POSIX.1-2024)

An implementation of the `link` utility of POSIX.1-2024 (XCU link), in C
with no dependencies beyond libc. It belongs to the XSI option.

    link file1 file2

## Build and test

    make           # builds ./link
    make test      # the tests (sh test_link.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* link(file1, file2) and its errors.
* A first "--" is discarded (XCU 1.4).

## Choices where the standard leaves room

* An argument starting with '-' before "--" is a usage error, as in unlink.

## Testing

`test_link.sh` runs the utility on prepared input and compares the output
and the exit status: a new link (the link count), existing targets, missing
files, directories, symbolic links (the link itself), the number of
operands, "--". The binary tested is `./link`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `link.c` | the utility |
| `test_link.sh` | the tests: `make test`, or `sh test_link.sh` |
| `Makefile` | build, test, install, clean |
