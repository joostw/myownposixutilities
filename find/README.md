# find — find files (POSIX.1-2024)

An implementation of the `find` utility of POSIX.1-2024 (XCU find), in C
with no dependencies beyond libc.

    find [-H|-L] path... [operand_expression...]

## Build and test

    make           # builds ./find
    make test      # the tests (sh test_find.sh)
    make difftest  # compares with GNU find
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* All the primaries of POSIX.1-2024 (-name, -iname, -path, -nouser,
  -nogroup, -mount, -xdev, -prune, -perm, -type, -links, -user, -group,
  -size, -atime, -ctime, -mtime, -exec ; and +, -ok, -print, -print0,
  -newer, -depth) and the operators.
* Hierarchies of any depth: fstatat() and openat() relative to open
  directories, at most 32 descriptors open, a closed level reopened as ".."
  and checked; the working directory never changes, so commands run where
  find was started.
* Directory loops (with -L) are reported and not entered.
* -exec ... {} + collects pathnames up to {ARG_MAX} less the environment.

## Choices where the standard leaves room

* Without path operands, "." is searched.
* -print0 also keeps the expression from getting an implicit -print.
* -exec ... ; replaces {} inside longer arguments too.
* -newer without -H or -L follows a link; -perm's symbolic mode is applied
  like chmod's, umask included.
* With -H or -L, a dangling link is acted on as the link; a loop of links is
  reported.
* The prompt of -ok is "< utility argument... >? ".

## Testing

`test_find.sh` runs the utility on prepared input and compares the output
and the exit status: every primary and operator, -prune and -depth,
pathnames as given (trailing slashes), -H and -L with dangling links and
loops, -exec ; and + (with many long names), -ok, -print0, a tree deeper
than {PATH_MAX} and 150 levels with 16 open files, -xdev and -mount across
/dev/shm when that is another file system, unreadable directories (not as
root), errors; outputs are sorted, as the order of directory entries is that
of readdir(). The binary tested is `./find`, or `$U` if set.

`tests/rfind.py` builds random trees and random expressions and compares the
sorted output with that of GNU find (run with -O0: its expression optimizer
can move -name ahead of -print); all cases are the same.

## Files

| file | contents |
|------|----------|
| `find.c` | the utility |
| `tests/rfind.py` | the random comparison (`make difftest`) |
| `test_find.sh` | the tests: `make test`, or `sh test_find.sh` |
| `Makefile` | build, test, install, clean |
