# pwd — return working directory name (POSIX.1-2024)

An implementation of the `pwd` utility of POSIX.1-2024 (XCU pwd), in C with
no dependencies beyond libc.

    pwd [-L|-P]

## Build and test

    make           # builds ./pwd
    make test      # the tests (sh test_pwd.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* -L (the default) writes PWD when it is an absolute name of the working
  directory without `.` or `..` components; otherwise, and with -P, the name
  from getcwd() without symbolic links.
* Directories deeper than PATH_MAX work with -P.
* A write error makes the exit status nonzero.

## Choices where the standard leaves room

* The last of -L and -P given is used.

## Testing

`test_pwd.sh` runs the utility on prepared input and compares the output and
the exit status: -L and -P with symbolic links, PWD that is wrong, relative
or has dot components, very deep directories, write errors. The binary
tested is `./pwd`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `pwd.c` | the utility |
| `test_pwd.sh` | the tests: `make test`, or `sh test_pwd.sh` |
| `Makefile` | build, test, install, clean |
