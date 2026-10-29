# fold — filter for folding lines (POSIX.1-2024)

An implementation of the `fold` utility of POSIX.1-2024 (XCU fold), in C
with no dependencies beyond libc.

    fold [-bs] [-w width] [file...]

## Build and test

    make           # builds ./fold
    make test      # the tests (sh test_fold.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Columns of the locale's characters (wcwidth()), never splitting a
  character, -b included; tab, backspace and carriage return as the standard
  says, with no break next to the latter two; -s breaks after the last blank
  of a segment.

## Choices where the standard leaves room

* Characters that are not printable, and bytes that are not characters,
  count one column.
* A width that is not a positive number is an error.

## Testing

`test_fold.sh` runs the utility on prepared input and compares the output
and the exit status: widths, -s, tabs, backspaces and carriage returns, -b,
missing newlines, UTF-8 characters and wide ones. Random lines agree with
GNU fold (in the C locale, with widths of at least 8 when there are tabs:
below that the standard leaves it undefined, and GNU fold then breaks next
to a backspace). The binary tested is `./fold`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `fold.c` | the utility |
| `test_fold.sh` | the tests: `make test`, or `sh test_fold.sh` |
| `Makefile` | build, test, install, clean |
