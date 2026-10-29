# sort — sort, merge, or sequence check text files (POSIX.1-2024)

An implementation of the `sort` utility of POSIX.1-2024 (XCU sort), in C
with no dependencies beyond libc.

    sort [-m] [-o output] [-bdfinru] [-t char] [-k keydef]... [file...]
    sort [-c|-C] [-bdfinru] [-t char] [-k keydef] [file]

## Build and test

    make           # builds ./sort
    make test      # the tests (sh test_sort.sh)
    make difftest  # compares with GNU sort
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Keys (-k with field and character positions and modifiers), -t, the global
  ordering options, -n with the locale's radix character and thousands
  separator, -u, -c and -C, -m (a merge as the files are read), -o onto an
  input file.
* Comparison by strcoll() (memcmp() in the POSIX collation), then by the
  whole line, then byte by byte, as the standard asks.
* Characters, not bytes: key positions count characters, and -d -f -i and -t
  work with multibyte characters.

## Choices where the standard leaves room

* The input is sorted in memory; there are no temporary files.
* Options may follow file operands (sort is excepted from Guideline 9); the
  obsolescent +pos form is not supported.
* Global ordering options apply to every key without modifiers of its own,
  wherever they are given.
* A key's start or end past the end of its field runs on into the line, as
  in GNU sort.
* -c reports "sort: FILE:N: disorder: LINE"; -u keeps the first of equal
  lines.

## Testing

`test_sort.sh` runs the utility on prepared input and compares the output
and the exit status: whole lines, every ordering option, -n in detail, keys
and fields with and without -t, ties, -u, several files, -o, -m, -c and -C
with their messages, errors, and multibyte characters. The binary tested is
`./sort`, or `$U` if set.

`tests/rsort.py` sorts random lines with random options and keys with this
sort and GNU sort and compares the output; -c on GNU sort's output, -C and
-m are checked too. In the C locale, and in C.UTF-8 on ASCII input, all
thousands of cases are the same (GNU sort counts key positions in bytes, so
input outside ASCII is not compared).

## Files

| file | contents |
|------|----------|
| `sort.c` | the utility |
| `tests/rsort.py` | the random comparison (`make difftest`) |
| `test_sort.sh` | the tests: `make test`, or `sh test_sort.sh` |
| `Makefile` | build, test, install, clean |
