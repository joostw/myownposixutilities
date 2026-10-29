# renice — set nice values of running processes (POSIX.1-2024)

An implementation of the `renice` utility of POSIX.1-2024 (XCU renice), in C
with no dependencies beyond libc.

    renice [-g|-p|-u] -n increment ID...

## Build and test

    make           # builds ./renice
    make test      # the tests (sh test_renice.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* For each ID: the nice value getpriority() gives (PRIO_PROCESS, PRIO_PGRP
  or PRIO_USER) plus the increment, kept within -NZERO and NZERO-1, set with
  setpriority().
* -u takes a login name or, if there is none, a user ID.

## Choices where the standard leaves room

* The options are read by hand (renice is excepted from Guideline 9): -g, -p
  and -u apply to the IDs after them, until the next one, as in historical
  renice; -n may come anywhere.
* For a process group or a user, the value of the process with the lowest
  nice value is incremented and set for all of them (as historical renice
  does).
* An ID that is not valid, a process that does not exist, a change that is
  not allowed: reported, the others still done, status 1. Usage errors:
  status 2.

## Testing

`test_renice.sh` runs the utility on prepared input and compares the output
and the exit status: the nice value of processes (by ps) for -p, -g (and the
group's other processes), -u (as root), several IDs, the highest value,
negative increments with and without privileges, errors, options after IDs.
The binary tested is `./renice`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `renice.c` | the utility |
| `test_renice.sh` | the tests: `make test`, or `sh test_renice.sh` |
| `Makefile` | build, test, install, clean |
