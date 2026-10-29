# expand — convert tabs to spaces (POSIX.1-2024)

An implementation of the `expand` utility of POSIX.1-2024 (XCU expand), in C
with no dependencies beyond libc.

    expand [-t tablist] [file...]

## Build and test

    make           # builds ./expand
    make test      # the tests (sh test_expand.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Tab stops every n columns or at a list of columns; past the last stop of a
  list a tab is one space; backspace goes back a column. Columns are display
  widths.

## Choices where the standard leaves room

* Bytes that are not characters, and control characters, count one column.

## Testing

`test_expand.sh` runs the utility on prepared input and compares the output
and the exit status: tabs, stops, lists with commas and blanks, past the
list, backspaces, missing newlines, bad lists. Random lines agree with GNU
expand. The binary tested is `./expand`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `expand.c` | the utility |
| `test_expand.sh` | the tests: `make test`, or `sh test_expand.sh` |
| `Makefile` | build, test, install, clean |
