# uuencode — encode a binary file (POSIX.1-2024)

An implementation of the `uuencode` utility of POSIX.1-2024 (XCU uuencode),
in C with no dependencies beyond libc.

    uuencode [-m] [file] decode_pathname

## Build and test

    make           # builds ./uuencode
    make test      # the tests (sh test_uuencode.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The historical encoding (lines of up to 45 bytes, "`" for 0) and, with -m,
  Base64 (lines of 57 bytes, "begin-base64" and "===="), with the mode of
  the file, or 0666 less the mask for the standard input.

## Choices where the standard leaves room

* The mode is written as "%03o" of the permission bits (with the set-ID and
  sticky bits when they are set).
* Usage errors exit with status 2; other errors with status 1.

## Testing

`test_uuencode.sh` runs the utility on prepared input and compares the
output and the exit status: both encodings decoded by Python (binascii and
base64) for many sizes, the line lengths, the header, the mode of a file and
of the standard input, an empty file, errors. The binary tested is
`./uuencode`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `uuencode.c` | the utility |
| `test_uuencode.sh` | the tests: `make test`, or `sh test_uuencode.sh` |
| `Makefile` | build, test, install, clean |
