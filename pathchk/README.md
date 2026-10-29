# pathchk — check pathnames (POSIX.1-2024)

An implementation of the `pathchk` utility of POSIX.1-2024 (XCU pathchk), in
C with no dependencies beyond libc.

    pathchk [-p] [-P] pathname...

## Build and test

    make           # builds ./pathchk
    make test      # the tests (sh test_pathchk.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Against the file system: {PATH_MAX}, {NAME_MAX} of the directory that
  contains (or would contain) each component, search permission of the
  directories passed, and existing components that are not directories but
  are followed by more.
* -p instead: {_POSIX_PATH_MAX}, {_POSIX_NAME_MAX} and the portable filename
  character set; -P in addition: empty pathnames and components starting
  with '-'.

## Choices where the standard leaves room

* {PATH_MAX} and {_POSIX_PATH_MAX} count the terminating null byte.
* No byte sequence is reported as not valid (the file systems take all but /
  and NUL).
* An empty pathname is an error also without -P.

## Testing

`test_pathchk.sh` runs the utility on prepared input and compares the output
and the exit status: good and missing pathnames, components of {NAME_MAX}
and longer, {PATH_MAX}, files as directories, -p limits and characters, -P,
unsearchable directories (not as root). The binary tested is `./pathchk`, or
`$U` if set.

## Files

| file | contents |
|------|----------|
| `pathchk.c` | the utility |
| `test_pathchk.sh` | the tests: `make test`, or `sh test_pathchk.sh` |
| `Makefile` | build, test, install, clean |
