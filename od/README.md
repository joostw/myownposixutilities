# od — dump files in various formats (POSIX.1-2024)

An implementation of the `od` utility of POSIX.1-2024 (XCU od), in C with no
dependencies beyond libc.

    od [-v] [-A address_base] [-j skip] [-N count] [-t type_string]... [file...]
    od [-bcdosx] [file] [[+]offset[.][b]]

## Build and test

    make           # builds ./od
    make test      # the tests (sh test_od.sh)
    make difftest  # compares with GNU od
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* All the types of -t (a, c, d o u x of 1, 2, 4 and 8 bytes or C S I L, f of
  float, double and long double), the XSI options -b -c -d -o -s -x in the
  order given, -A, -j with b k m multipliers, -N, -v, and the XSI offset
  operand.
* The layout of GNU od: 16 bytes a line, each type's fields aligned with
  those of the others; repeated blocks as `*`; floating values in the
  shortest form that reads back exactly.
* -c in a multibyte locale: a character in its first byte's area and `**` in
  the others, as the standard says.

## Choices where the standard leaves room

* The offset is octal unless -A says otherwise, with 7 digits (6 in
  hexadecimal); with -A n there is no final offset.
* Byte order: that of the machine; d o u x default to int, f to double.
* -c: a character cut by -j, -N or the end, or bytes that are not one, are
  written in octal.

## Testing

`test_od.sh` runs the utility on prepared input and compares the output and
the exit status: 60 cases whose expected output is that of GNU od, for every
type and option, concatenated files, -j and -N, the offset operand (also
"10.", which GNU od does not take), errors; -c with UTF-8. The cases with
items of more than one byte expect little-endian order and are skipped
otherwise. The binary tested is `./od`, or `$U` if set.

`tests/oddiff.sh` runs about 340 combinations of options and inputs through
this od and GNU od and compares the output and the exit status; all are the
same.

## Files

| file | contents |
|------|----------|
| `od.c` | the utility |
| `tests/oddiff.sh` | the comparison with GNU od (`make difftest`) |
| `test_od.sh` | the tests: `make test`, or `sh test_od.sh` |
| `Makefile` | build, test, install, clean |
