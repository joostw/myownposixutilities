# uname — return system name (POSIX.1-2024)

An implementation of the `uname` utility of POSIX.1-2024 (XCU uname), in C
with no dependencies beyond libc.

    uname [-amnrsv]

## Build and test

    make           # builds ./uname
    make test      # the tests (sh test_uname.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The fields of uname(), in the order of the standard (sysname, nodename,
  release, version, machine) whatever the order of the options; -a is all
  five, no option -s.

## Testing

`test_uname.sh` runs the utility on prepared input and compares the output
and the exit status: each option and combinations against the system's
uname, operands, usage errors, write errors. The binary tested is `./uname`,
or `$U` if set.

## Files

| file | contents |
|------|----------|
| `uname.c` | the utility |
| `test_uname.sh` | the tests: `make test`, or `sh test_uname.sh` |
| `Makefile` | build, test, install, clean |
