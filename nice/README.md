# nice — invoke a utility with an altered nice value (POSIX.1-2024)

An implementation of the `nice` utility of POSIX.1-2024 (XCU nice), in C
with no dependencies beyond libc.

    nice [-n increment] utility [argument...]

## Build and test

    make           # builds ./nice
    make test      # the tests (sh test_nice.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* nice() with the increment, then the utility.
* The utility is searched for in PATH as execvp() does; a file that is not a
  program (ENOEXEC) is run by sh; the status is 127 when it is not found (a
  directory in PATH that cannot be searched does not count as finding it)
  and 126 when it is found but cannot be executed.

## Choices where the standard leaves room

* Without -n the increment is 10.
* When nice() fails (a negative increment without privileges) a warning is
  written and the utility is run anyway.
* Errors of nice itself exit with 125.

## Testing

`test_nice.sh` runs the utility on prepared input and compares the output
and the exit status: the nice value the utility gets (read with Python) for
default, positive, signed and large increments, nested nice; a negative
increment as root and without privileges; statuses 126 and 127, scripts
without #!, invalid increments. The binary tested is `./nice`, or `$U` if
set.

## Files

| file | contents |
|------|----------|
| `nice.c` | the utility |
| `test_nice.sh` | the tests: `make test`, or `sh test_nice.sh` |
| `Makefile` | build, test, install, clean |
