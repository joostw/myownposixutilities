# chown — change the file ownership (POSIX.1-2024)

An implementation of the `chown` utility of POSIX.1-2024 (XCU chown), in C
with no dependencies beyond libc.

    chown [-h] owner[:group] file...
    chown -R [-H|-L|-P] owner[:group] file...

## Build and test

    make           # builds ./chown
    make test      # the tests (sh test_chown.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Names from the user and group databases, or numbers; -h changes a symbolic
  link itself.
* -R walks the hierarchy relative to open directories (any depth, at most 32
  descriptors open); -H, -L and -P as the standard says; loops met with -L
  are reported and not entered.
* The set-ID bits are cleared by chown() itself where the system does so.

## Choices where the standard leaves room

* -R without -H, -L or -P acts as -P.
* With -R and -H or -L, a link whose target does not exist is changed
  itself; without -R, a dangling link operand is an error (chown() fails).
* A name is looked up before a string of digits is taken as a number; the
  owner must not be empty, nor the group after a colon.

## Testing

`test_chown.sh` runs the utility on prepared input and compares the output
and the exit status: own user and group by number and name, unknown names,
empty parts; as root: owners and groups, links with and without -h, -R with
-H, -L and -P, loops, a tree deeper than {PATH_MAX} with 16 open files; as
another user: refusals. The binary tested is `./chown`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `chown.c` | the utility |
| `test_chown.sh` | the tests: `make test`, or `sh test_chown.sh` |
| `Makefile` | build, test, install, clean |
