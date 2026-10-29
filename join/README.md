# join — relational database operator (POSIX.1-2024)

An implementation of the `join` utility of POSIX.1-2024 (XCU join), in C
with no dependencies beyond libc.

    join [-a file_number|-v file_number] [-e string] [-o list] [-t char]
         [-1 field] [-2 field] file1 file2

## Build and test

    make           # builds ./join
    make test      # the tests (sh test_join.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* A merge of the two files a group of equal join fields at a time, all
  combinations in the order of the lines; join fields compared with
  strcoll().
* -a, -v, -e, -o (commas or blanks, 0 for the join field), -t (every
  separator counts), -1 and -2.

## Choices where the standard leaves room

* Unpairable lines are written like paired ones without the other file's
  fields.
* -e is used only with -o.
* Several -o options are joined into one list.

## Testing

`test_join.sh` runs the utility on prepared input and compares the output
and the exit status: the default output, -a and -v, -o with 0 and missing
fields, -e, -t with empty fields, -1 and -2, leading blanks, standard input,
errors (the same output as GNU join). The binary tested is `./join`, or `$U`
if set.

## Files

| file | contents |
|------|----------|
| `join.c` | the utility |
| `test_join.sh` | the tests: `make test`, or `sh test_join.sh` |
| `Makefile` | build, test, install, clean |
