# sleep — suspend execution for an interval (POSIX.1-2024)

An implementation of the `sleep` utility of POSIX.1-2024 (XCU sleep), in C
with no dependencies beyond libc.

    sleep time

## Build and test

    make           # builds ./sleep
    make test      # the tests (sh test_sleep.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* time is a non-negative decimal integer of any length; a value too large to
  represent sleeps for the longest representable interval.
* An interrupted sleep (by a signal that is caught) resumes.

## Choices where the standard leaves room

* SIGALRM is ignored, one of the three actions the standard allows.

## Testing

`test_sleep.sh` runs the utility on prepared input and compares the output
and the exit status: zero and short times, `--`, operand errors (signs,
blanks, fractions), huge values, SIGALRM. The binary tested is `./sleep`, or
`$U` if set.

## Files

| file | contents |
|------|----------|
| `sleep.c` | the utility |
| `test_sleep.sh` | the tests: `make test`, or `sh test_sleep.sh` |
| `Makefile` | build, test, install, clean |
