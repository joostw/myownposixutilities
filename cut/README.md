# cut — cut out selected fields of each line of a file (POSIX.1-2024)

An implementation of the `cut` utility of POSIX.1-2024 (XCU cut), in C with
no dependencies beyond libc.

    cut -b list [-n] [file...]
    cut -c list [file...]
    cut -f list [-d delim] [-s] [file...]

## Build and test

    make           # builds ./cut
    make test      # the tests (sh test_cut.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Lists of numbers and ranges (`n`, `lo-hi`, `-hi`, `lo-`), separated by
  commas or blanks, in any order and overlapping; what they select is
  written once, in input order. Lists and lines are of unlimited length.
* -c counts the characters of the locale; -b counts bytes.
* -b -n adjusts each list element per line as the standard describes, so
  that no character is split.
* -d takes one character, which may be a multibyte one; -s suppresses lines
  without it.
* A last line without a newline is written with one.

## Choices where the standard leaves room

* A decreasing range (`5-2`) is an error.
* A byte that does not begin a valid character counts as one character for
  -c.

## Testing

`test_cut.sh` runs the utility on prepared input and compares the output and
the exit status: the three modes, the list syntax and its errors, -n on
UTF-8 text, multibyte delimiters, -s, several files, long lines, write
errors. The binary tested is `./cut`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `cut.c` | the utility |
| `test_cut.sh` | the tests: `make test`, or `sh test_cut.sh` |
| `Makefile` | build, test, install, clean |
