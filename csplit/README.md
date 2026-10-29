# csplit — split files based on context (POSIX.1-2024)

An implementation of the `csplit` utility of POSIX.1-2024 (XCU csplit), in C
with no dependencies beyond libc.

    csplit [-ks] [-f prefix] [-n number] file arg...

## Build and test

    make           # builds ./csplit
    make test      # the tests (sh test_csplit.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The operands /rexp/[offset], %rexp%[offset], line_no and {num} (a repeat
  of the operand before it); the pieces are prefix (xx) followed by number
  (2) digits.
* The size of each piece in bytes is written, unless -s; on an error the
  pieces made are removed, unless -k.

## Choices where the standard leaves room

* The file is read into memory, so that the pieces can be found before any
  is written.
* The first search starts at line 1; a line number equal to the current line
  makes an empty piece; one past the end is an error.
* The last piece is written even when it is empty.
* Usage errors exit with status 2; other errors with status 1.

## Testing

`test_csplit.sh` runs the utility on prepared input and compares the output
and the exit status: regular expressions with offsets up and down, skipping
with %rexp%, line numbers, repeats, -k, -s, -f, -n, errors and the removal
of the pieces. The binary tested is `./csplit`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `csplit.c` | the utility |
| `test_csplit.sh` | the tests: `make test`, or `sh test_csplit.sh` |
| `Makefile` | build, test, install, clean |
