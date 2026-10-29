# nl — line numbering filter (POSIX.1-2024)

An implementation of the `nl` utility of POSIX.1-2024 (XCU nl), in C with no
dependencies beyond libc. It belongs to the XSI option.

    nl [-p] [-b type] [-d delim] [-f type] [-h type] [-i incr] [-l num]
       [-n format] [-s sep] [-v startnum] [-w width] [file]

## Build and test

    make           # builds ./nl
    make test      # the tests (sh test_nl.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Logical pages of header, body and footer with their delimiter lines; the
  types a, t, n and pBRE for each section; -i, -l, -n, -p, -s, -v and -w.

## Choices where the standard leaves room

* A delimiter line is written as an empty line.
* A new page (where the numbering starts again unless -p) begins with a
  header, a body that does not follow a header, or a footer that follows
  neither; GNU nl starts again at every delimiter.
* A line that is not numbered is written as it is, without number and
  separator, as the standard says; GNU nl indents it.
* -d with one character keeps ':' as the second.

## Testing

`test_nl.sh` runs the utility on prepared input and compares the output and
the exit status: every type, format and option, -l, sections and pages, -p,
-d, missing newlines, errors. The binary tested is `./nl`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `nl.c` | the utility |
| `test_nl.sh` | the tests: `make test`, or `sh test_nl.sh` |
| `Makefile` | build, test, install, clean |
