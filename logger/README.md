# logger — log messages (POSIX.1-2024)

An implementation of the `logger` utility of POSIX.1-2024 (XCU logger), in C
with no dependencies beyond libc.

    logger [-i] [-f file] [-p priority] [-t tag] [string...]

## Build and test

    make           # builds ./logger
    make test      # the tests (sh test_logger.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Each message is sent to the system log daemon's socket /dev/log as
  syslog() would send it, "<PRI>Mmm dd hh:mm:ss TAG[PID]: MESSAGE" (RFC
  3164), as a datagram, or NUL-ended on a stream socket. logger does this
  itself, not with syslog(), so that a message that cannot be sent is
  reported.
* The facilities user and local0 to local7 (and the others of <syslog.h>),
  and the levels of the standard, in any case.

## Choices where the standard leaves room

* The default tag is the login name (getlogin(), else the user's name).
* Lines of the standard input or the file: a message each; empty lines are
  not logged. The string operands are one message.
* A priority may be a level alone (facility user); the historical names
  error, warn and panic are accepted.
* No log daemon, an unknown priority: an error (status 1).
* -u socket sends to that socket instead of /dev/log (an extension, as in
  util-linux).

## Testing

`test_logger.sh` runs the utility on prepared input and compares the output
and the exit status: a log daemon of the tests' own (Python, a datagram and
a stream socket): the priorities, the tag, -i, the time stamp, lines of the
standard input and of -f, empty lines, a long message, no daemon, errors.
The binary tested is `./logger`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `logger.c` | the utility |
| `test_logger.sh` | the tests: `make test`, or `sh test_logger.sh` |
| `Makefile` | build, test, install, clean |
