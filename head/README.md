# head — copy the first part of files (POSIX.1-2024)

An implementation of the `head` utility of POSIX.1-2024 (XCU head), in C
with no dependencies beyond libc.

    head [-c number|-n number] [file...]

## Build and test

    make           # builds ./head
    make test      # the tests (sh test_head.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* -n counts lines, -c bytes (the default is -n 10); the count is a positive
  decimal number.
* With several files each is preceded by a `==> name <==` header; `-` names
  the standard input, and may be given more than once.
* Read and write errors make the exit status nonzero.

## Choices where the standard leaves room

* The last of -c and -n given is used.

## Testing

`test_head.sh` runs the utility on prepared input and compares the output
and the exit status: -n and -c, counts larger than the file, files without a
final newline, the standard input, headers for several files and for `-`,
count errors, binary data, write errors. The binary tested is `./head`, or
`$U` if set.

## Files

| file | contents |
|------|----------|
| `head.c` | the utility |
| `test_head.sh` | the tests: `make test`, or `sh test_head.sh` |
| `Makefile` | build, test, install, clean |
