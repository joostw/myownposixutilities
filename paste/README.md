# paste — merge corresponding or subsequent lines of files (POSIX.1-2024)

An implementation of the `paste` utility of POSIX.1-2024 (XCU paste), in C
with no dependencies beyond libc.

    paste [-s] [-d list] file...

## Build and test

    make           # builds ./paste
    make test      # the tests (sh test_paste.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Parallel and (-s) serial merging; each "-" takes the next line of the
  standard input in turn; -d elements are characters (multibyte ones too) or
  \n, \t, \\ and \0, used circularly and reset as the standard says.

## Choices where the standard leaves room

* A backslash before another character in -d stands for that character.
* Without operands the standard input is read.

## Testing

`test_paste.sh` runs the utility on prepared input and compares the output
and the exit status: uneven files, -d lists and escapes, -s, empty files,
"-" once and several times, missing newlines, multibyte delimiters (the same
output as GNU paste). The binary tested is `./paste`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `paste.c` | the utility |
| `test_paste.sh` | the tests: `make test`, or `sh test_paste.sh` |
| `Makefile` | build, test, install, clean |
