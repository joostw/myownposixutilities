# grep — search files for a pattern (POSIX.1-2024)

An implementation of the `grep` utility of POSIX.1-2024 (XCU grep), in C
with no dependencies beyond libc.

    grep [-E|-F] [-c|-l|-q] [-insvx] -e pattern_list [-e pattern_list]... [-f pattern_file]... [file...]
    grep [-E|-F] [-c|-l|-q] [-insvx] [-e pattern_list]... -f pattern_file [-f pattern_file]... [file...]
    grep [-E|-F] [-c|-l|-q] [-insvx] pattern_list [file...]

## Build and test

    make           # builds ./grep
    make test      # the tests (sh test_grep.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* BREs, EREs (-E) and fixed strings (-F) with regcomp() and regexec() of the
  locale; -i and -x for all three, -F case-insensitively by wide characters.
* Pattern lists from -e, -f and the operand; a newline separates patterns,
  so a trailing newline adds an empty pattern, which matches every line.
* -q writes nothing; otherwise -l takes precedence over -c, whatever their
  order.
* File names prefix the lines when there is more than one file operand.
* Exit status 0, 1 or 2; with -q, 0 as soon as a line is selected, even
  after an error.

## Choices where the standard leaves room

* -s silences the messages about files that do not exist or cannot be read,
  which includes directories.
* A selected last line without a newline is written with one.

## Testing

`test_grep.sh` runs the utility on prepared input and compares the output
and the exit status: BREs, EREs and fixed strings, every option and their
combinations, pattern lists and files, empty patterns, several files and
`-`, UTF-8 matching, errors and exit statuses. The binary tested is
`./grep`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `grep.c` | the utility |
| `posix_re.c` | matching a line against a set of patterns |
| `posix_re.h` | its interface |
| `test_grep.sh` | the tests: `make test`, or `sh test_grep.sh` |
| `Makefile` | build, test, install, clean |
