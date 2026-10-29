# getconf — get configuration values (POSIX.1-2024)

An implementation of the `getconf` utility of POSIX.1-2024 (XCU getconf), in
C with no dependencies beyond libc.

    getconf [-v specification] system_var
    getconf [-v specification] path_var pathname

## Build and test

    make           # builds ./getconf
    make test      # the tests (sh test_getconf.sh)
    make difftest  # compares with glibc's getconf
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Every variable the standard names: those of the tables of sysconf() (but
  the ones it leaves out), NPROCESSORS_CONF and NPROCESSORS_ONLN, those of
  confstr() without _CS_, the maximum and minimum values of <limits.h>
  (fixed by the standard), the earlier POSIX2_ names, and the path variables
  of pathconf().
* A name the system's headers do not have is valid and "undefined".

## Choices where the standard leaves room

* A variable whose function returns -1 without errno or with EINVAL is
  "undefined", options included.
* A V8 compilation environment variable the system has no name for
  (_POSIX_V8_LP64_OFF64, POSIX_V8_LP64_OFF64_CFLAGS, V8_ENV, ...) has the
  value of the V7 one.
* -v takes the names of the compilation environments of c17 (V8 and V7); the
  values are those of the environment getconf was compiled in, and another
  one is an error.
* Unknown names, a system variable with a pathname and a path variable
  without one are errors (status 1, nothing written); usage errors have
  status 2.

## Testing

`test_getconf.sh` runs the utility on prepared input and compares the output
and the exit status: every variable that Python's os.sysconf(), os.confstr()
and os.pathconf() know, compared with them; a few against the system's
getconf; the fixed values; the earlier names; the V8 environments and -v;
errors. The binary tested is `./getconf`, or `$U` if set.

`tests/gcdiff.sh` compares every variable glibc's getconf knows (the path
variables for four pathnames). Not compared are the minimum values of
<limits.h> that glibc takes from sysconf() instead of the fixed values of
the standard (_POSIX_ARG_MAX, _POSIX_OPEN_MAX, ...).

## Files

| file | contents |
|------|----------|
| `getconf.c` | the utility |
| `tests/gcdiff.sh` | the comparison with glibc's getconf (`make difftest`) |
| `test_getconf.sh` | the tests: `make test`, or `sh test_getconf.sh` |
| `Makefile` | build, test, install, clean |
