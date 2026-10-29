# strings — find printable strings in files (POSIX.1-2024)

An implementation of the `strings` utility of POSIX.1-2024 (XCU strings), in
C with no dependencies beyond libc.

    strings [-a] [-t format] [-n number] [file...]

## Build and test

    make           # builds ./strings
    make test      # the tests (sh test_strings.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Each run of at least number (4) printable characters is written on a line
  of its own; characters are decoded with mbrtowc() in the locale and tested
  with iswprint(), so a multibyte character counts as one; a tab is
  printable, a newline or a null byte ends a string.
* -t d, o or x writes the offset (in bytes) of each string before it.
* -a looks at the whole file (as without it: there is no object format to
  know about).

## Choices where the standard leaves room

* The whole file is always looked at, with or without -a.
* The offset is written without padding, followed by a space.
* A -n that is not a positive number, or a -t that is not d, o or x, is a
  usage error (status 2).

## Testing

`test_strings.sh` runs the utility on prepared input and compares the output
and the exit status: the minimum length, -n, the three offset formats, tabs,
newlines and null bytes, multibyte characters in a UTF-8 locale, several
files and the standard input, errors. The binary tested is `./strings`, or
`$U` if set.

## Files

| file | contents |
|------|----------|
| `strings.c` | the utility |
| `test_strings.sh` | the tests: `make test`, or `sh test_strings.sh` |
| `Makefile` | build, test, install, clean |
