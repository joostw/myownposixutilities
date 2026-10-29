# iconv — codeset conversion (POSIX.1-2024)

An implementation of the `iconv` utility of POSIX.1-2024 (XCU iconv), in C
with no dependencies beyond libc.

    iconv [-cs] -f frommap -t tomap [file...]
    iconv -f fromcode [-cs] [-t tocode] [file...]
    iconv -t tocode [-cs] [-f fromcode] [file...]
    iconv -l

## Build and test

    make           # builds ./iconv
    make test      # the tests (sh test_iconv.sh)
    make difftest  # compares with the C library's iconv
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Codesets named without a slash are those of iconv_open(); the text goes
  from the input codeset to UTF-32 and from there to the output codeset, so
  that a character that is not valid input and one that the output codeset
  lacks can be told apart (and skipped whole with -c).
* A name with a slash is a charmap (XBD 6.4, with the escape and comment
  characters, decimal and hexadecimal ranges); two charmaps are joined on
  their symbolic names, byte sequences found by the longest match.
* A missing -f or -t is the codeset of the locale.

## Choices where the standard leaves room

* A bad character is reported (unless -s) with its byte offset, or as U+XXXX
  when the output codeset lacks it; without -c the conversion of that file
  stops there, with -c it is left out. Either way the status is 1.
* Each file starts in the initial shift state, and the output is brought
  back to it at the end of each file.
* Only two charmaps can be joined: a charmap with a codeset name is an
  error.
* -l writes the names the system accepts, from a list of its own and the
  gconv-modules files of glibc, one a line (not those with a slash).
* Usage errors exit with status 2; other errors with status 1.

## Testing

`test_iconv.sh` runs the utility on prepared input and compares the output
and the exit status: conversions between Unicode forms and legacy codesets,
a stateful codeset, big files and characters across reads, the locale's
codeset, invalid and missing characters with and without -c and -s, charmaps
(ranges, escapes, multibyte, errors), -l. The binary tested is `./iconv`, or
`$U` if set.

`tests/iconvdiff.sh` converts texts to about 30 codesets and back with both
iconvs, and converts with the charmaps of /usr/share/i18n/charmaps joined
with the UTF-8 charmap, against the conversions by name.

## Files

| file | contents |
|------|----------|
| `iconv.c` | the utility |
| `tests/iconvdiff.sh` | the comparison with the iconv of glibc (`make difftest`) |
| `test_iconv.sh` | the tests: `make test`, or `sh test_iconv.sh` |
| `Makefile` | build, test, install, clean |
