# time — time a simple command (POSIX.1-2024)

An implementation of the `time` utility of POSIX.1-2024 (XCU time), in C
with no dependencies beyond libc.

    time [-p] utility [argument...]

## Build and test

    make           # builds ./time
    make test      # the tests (sh test_time.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The utility in a child process; the elapsed time from CLOCK_MONOTONIC, the
  user and system times from the children's times of times(), written to the
  standard error as "real %f\nuser %f\nsys %f\n".
* SIGINT and SIGQUIT are ignored by time while it waits; the utility has
  them as time found them.
* The utility is searched for in PATH as execvp() does; a file that is not a
  program (ENOEXEC) is run by sh; the status is 127 when it is not found (a
  directory in PATH that cannot be searched does not count as finding it)
  and 126 when it is found but cannot be executed.

## Choices where the standard leaves room

* Without -p the format is that of -p, with two digits after the radix
  character (more when the clock has more than 100 ticks a second).
* A utility ended by a signal: a message, the times, then time ends itself
  by the same signal (with no core image), so that its status is the
  utility's.
* Errors of time itself exit with 125.

## Testing

`test_time.sh` runs the utility on prepared input and compares the output
and the exit status: the format, the elapsed time of sleep 1, the processor
time of a busy utility (and of its children), the status (also of a utility
killed by a signal), SIGINT and SIGQUIT to time, statuses 126 and 127. The
binary tested is `./time`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `time.c` | the utility |
| `test_time.sh` | the tests: `make test`, or `sh test_time.sh` |
| `Makefile` | build, test, install, clean |
