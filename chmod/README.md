# chmod — change the file modes (POSIX.1-2024)

An implementation of the `chmod` utility of POSIX.1-2024 (XCU chmod), in C
with no dependencies beyond libc.

    chmod [-R] mode file...

## Build and test

    make           # builds ./chmod
    make test      # the tests (sh test_chmod.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Octal modes, and symbolic modes by the grammar of the standard (a permcopy
  is one letter alone: `u=gw` is an error). The mode is compiled once and
  applied to each file from its own mode; without a *who*, the bits of the
  umask are left alone.
* X is decided by the mode before any change, or by the file being a
  directory.
* The mode may start with `-` (`chmod -w f`); only -R is an option.
* -R walks the hierarchy relative to open directories: no limit on depth or
  pathname length, at most 64 descriptors open.

## Choices where the standard leaves room

* Set-ID requests are honoured for every file type, also without execute
  bits; an octal mode sets all twelve bits, for directories too; clearing
  the execute bits leaves the set-ID bits alone.
* t works when the *who* is empty or includes o; with only u or g it is
  ignored.
* -R follows symbolic links named as operands; those met in the hierarchy
  are neither followed nor changed.
* -R changes a directory before its contents when the new mode lets the
  owner read and search it, after them otherwise, so that both opening and
  closing a hierarchy work without privileges.

## Testing

`test_chmod.sh` runs the utility on prepared input and compares the output
and the exit status: about sixty modes against several umasks (the symbolic
results agree with GNU chmod, which however decides X by the mode as changed
so far), X, directories, invalid modes, -R on trees with symbolic links,
deeper than {PATH_MAX} with 16 open files, and (not as root) -R a-rwx and
u+rwx on closed trees. The binary tested is `./chmod`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `chmod.c` | the utility |
| `test_chmod.sh` | the tests: `make test`, or `sh test_chmod.sh` |
| `Makefile` | build, test, install, clean |
