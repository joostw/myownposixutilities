# cmp — compare two files (POSIX.1-2024)

An implementation of the `cmp` utility of POSIX.1-2024 (XCU cmp), in C with
no dependencies beyond libc.

    cmp [-l|-s] file1 file2

## Build and test

    make           # builds ./cmp
    make test      # the tests (sh test_cmp.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The default, -l and -s output of the standard, the EOF message when one
  file is a prefix of the other, and the exit statuses 0, 1 and 2.
* Files are read in blocks; byte and line numbers are uintmax_t.

## Choices where the standard leaves room

* -l with -s is a usage error.
* The EOF message ends in " after byte N, line L" (or " (empty)").
* With -s, errors are still reported on standard error.
* Two operands "-", or the same file, are identical.

## Testing

`test_cmp.sh` runs the utility on prepared input and compares the output and
the exit status: equal and different files, -l, -s, prefixes and empty files
(with the messages), standard input, binary bytes, large files, missing
files and directories, usage errors, write errors. The binary tested is
`./cmp`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `cmp.c` | the utility |
| `test_cmp.sh` | the tests: `make test`, or `sh test_cmp.sh` |
| `Makefile` | build, test, install, clean |
