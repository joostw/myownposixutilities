# ln — link files (POSIX.1-2024)

An implementation of the `ln` utility of POSIX.1-2024 (XCU ln), in C with no
dependencies beyond libc.

    ln [-fs] [-L|-P] source_file target_file
    ln [-fs] [-L|-P] source_file... target_dir

## Build and test

    make           # builds ./ln
    make test      # the tests (sh test_ln.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Hard and symbolic links, into a directory or to a name; -f removes an
  existing target first.
* -L and -P decide whether a hard link to a symbolic link links its target
  or the link itself.
* The check that source and target are the same compares directory entries,
  as the standard says, so a second hard link to a file is not refused.

## Choices where the standard leaves room

* Without -L or -P, a hard link to a symbolic link links the link itself, as
  with -P (the default is implementation-defined).

## Testing

`test_ln.sh` runs the utility on prepared input and compares the output and
the exit status: hard and symbolic links, -f, links into directories, -L and
-P, the same-entry check, errors. The binary tested is `./ln`, or `$U` if
set.

## Files

| file | contents |
|------|----------|
| `ln.c` | the utility |
| `test_ln.sh` | the tests: `make test`, or `sh test_ln.sh` |
| `Makefile` | build, test, install, clean |
