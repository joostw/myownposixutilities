# realpath — resolve a pathname (POSIX.1-2024)

An implementation of the `realpath` utility of POSIX.1-2024 (XCU realpath),
in C with no dependencies beyond libc.

    realpath [-E|-e] file

## Build and test

    make           # builds ./realpath
    make test      # the tests (sh test_realpath.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* realpath() when it succeeds.
* With -E, and an ENOENT error: the directory part is resolved by realpath()
  and must be a directory, the last component (trailing slashes ignored) is
  appended; a dangling symbolic link there is followed, as often as it takes
  (up to {SYMLOOP_MAX}).
* -e: realpath() must succeed. Of -E and -e the last counts.

## Choices where the standard leaves room

* Without options realpath acts as with -E (the last component need not
  exist).

## Testing

`test_realpath.sh` runs the utility on prepared input and compares the
output and the exit status: directories, dot and dot-dot, links on the way
and at the end, missing last components with -E and -e, dangling links,
trailing slashes, loops, files used as directories, empty operands. The
binary tested is `./realpath`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `realpath.c` | the utility |
| `test_realpath.sh` | the tests: `make test`, or `sh test_realpath.sh` |
| `Makefile` | build, test, install, clean |
