# test — evaluate expression (POSIX.1-2024)

An implementation of the `test` utility of POSIX.1-2024 (XCU test), in C
with no dependencies beyond libc.

    test [expression]
    [ [expression] ]

## Build and test

    make           # builds ./test
    make test      # the tests (sh test_test.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The algorithm of the standard by the number of arguments (0 to 4), with
  all the primaries of POSIX.1-2024, < and > (by strcoll()) included.
* `[` is a link to `test`; called by that name, the last argument must be
  `]`.
* -r, -w and -x use the effective IDs (faccessat() with AT_EACCESS); -nt and
  -ot compare modification times with their nanoseconds.

## Choices where the standard leaves room

* Beyond what the standard specifies (more than four arguments, and the
  forms it leaves unspecified), the historical grammar: ! before -a before
  -o, and ( ). `( x )` and `( a b )` are read that way.
* Integers are decimal, with blanks around them and a sign; anything else,
  or one out of range, is an error (status 2).
* There are no options: "--" is a string.

## Testing

`test_test.sh` runs the utility on prepared input and compares the output
and the exit status: every primary, the cases for 0 to 4 arguments, -a, -o
and parentheses, integers and their errors, file types and permissions (also
as an ordinary user), -nt, -ot and -ef, and `[` with and without `]`; each
case is run both as test and as [. The binary tested is `./test`, or `$U` if
set.

## Files

| file | contents |
|------|----------|
| `test.c` | the utility |
| `[` | a link to test, made by make (target lbracket) |
| `prog.made` | made by make: when the program was last linked |
| `test_test.sh` | the tests: `make test`, or `sh test_test.sh` |
| `Makefile` | build, test, install, clean |
