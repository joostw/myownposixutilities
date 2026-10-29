# chgrp — change the file group ownership (POSIX.1-2024)

An implementation of the `chgrp` utility of POSIX.1-2024 (XCU chgrp), in C
with no dependencies beyond libc.

    chgrp [-h] group file...
    chgrp -R [-H|-L|-P] group file...

## Build and test

    make           # builds ./chgrp
    make test      # the tests (sh test_chgrp.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The program of chown for the group only (the owner is left as it is); -h,
  -R, -H, -L and -P as there.

## Choices where the standard leaves room

* -R without -H, -L or -P acts as -P.
* A name is looked up before a string of digits is taken as a number.

## Testing

`test_chgrp.sh` runs the utility on prepared input and compares the output
and the exit status: own group by number and name, unknown and empty names;
as root: groups, links with and without -h, -R with -H, -L and -P; as
another user: a group one is not in. The binary tested is `./chgrp`, or `$U`
if set.

## Files

| file | contents |
|------|----------|
| `chgrp.c` | the utility |
| `test_chgrp.sh` | the tests: `make test`, or `sh test_chgrp.sh` |
| `Makefile` | build, test, install, clean |
