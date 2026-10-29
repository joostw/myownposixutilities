# tr — translate characters (POSIX.1-2024)

An implementation of the `tr` utility of POSIX.1-2024 (XCU tr), in C with no
dependencies beyond libc.

    tr [-c|-C] [-s] string1 string2
    tr -s [-c|-C] string1
    tr -d [-c|-C] string1
    tr -ds [-c|-C] string1 string2

## Build and test

    make           # builds ./tr
    make test      # the tests (sh test_tr.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Characters of the locale, multibyte ones included; a byte that is not a
  character is a value of its own.
* Escapes, `\octal` (in a row, they can encode a multibyte character),
  ranges, `[:class:]` for every class of the locale, `[=equiv=]`, `[x*n]`
  (octal with a leading zero; omitted or zero, it fills string2).
* `[:lower:]`/`[:upper:]` at the same position in both strings is case
  conversion.
* -c orders the complement by value, -C by collation; -s squeezes the
  characters of the last operand's array.

## Choices where the standard leaves room

* A string2 shorter than string1 is padded with its last character (as BSD
  does).
* A character given twice in string1 takes the last mapping.
* A decreasing range (`z-a`) is an error.
* `[=c=]` holds the characters of the same primary collation weight where
  the C library shows it (glibc), otherwise just c.

## Testing

`test_tr.sh` runs the utility on prepared input and compares the output and
the exit status: translation, deletion and squeezing with each kind of
string, complements, case conversion, `[x*n]`, NULs, bytes that are not
characters, UTF-8, operand errors, large input, write errors. The binary
tested is `./tr`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `tr.c` | the utility |
| `test_tr.sh` | the tests: `make test`, or `sh test_tr.sh` |
| `Makefile` | build, test, install, clean |
