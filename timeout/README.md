# timeout — execute a utility with a time limit (POSIX.1-2024)

An implementation of the `timeout` utility of POSIX.1-2024 (XCU timeout), in
C with no dependencies beyond libc.

    timeout [-fp] [-k time] [-s signal_name] duration utility [argument...]

## Build and test

    make           # builds ./timeout
    make test      # the tests (sh test_timeout.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* timeout makes itself a process group leader (unless -f), runs the utility
  in a child process, and waits with sigtimedwait() for SIGCHLD, for the
  signals it passes on and for the time limit; the signal goes to the
  process group with its disposition in timeout set to be ignored while it
  is sent (to the child only with -f), and SIGCONT follows it.
* Durations and -k times: decimal numbers with a fraction and the suffixes
  s, m, h and d; 0 is no limit. -k sends SIGKILL that long after the first
  signal (the one sent at the time limit or one passed on).
* The utility gets the dispositions timeout inherited, but the default for
  the signal it will be sent; timeout ignores SIGTTIN and SIGTTOU.
* Exit status 124 at the time limit (without -p), else the utility's, or its
  signal: timeout ends itself by it, with no core image.
* The utility is searched for in PATH as execvp() does; a file that is not a
  program (ENOEXEC) is run by sh; the status is 127 when it is not found (a
  directory in PATH that cannot be searched does not count as finding it)
  and 126 when it is found but cannot be executed.

## Choices where the standard leaves room

* SIGKILL and SIGSTOP cannot be ignored: to send them to its process group,
  timeout first goes back to the process group it was in, so the status of
  -s KILL and of -k is 124. When timeout led its group from the start (a job
  of an interactive shell), SIGKILL ends it too (the status is then as with
  -p).
* SIGCONT is sent after every signal, not only to a stopped utility.
* The signals passed on: SIGHUP, SIGINT, SIGQUIT, SIGALRM, SIGTERM, SIGUSR1,
  SIGUSR2, SIGPIPE, SIGXCPU, SIGXFSZ, SIGVTALRM and SIGPROF when not
  inherited as ignored, and the -s signal.
* -s takes the name also with SIG before it, and a signal number.
* A duration may start with its fraction (".5").

## Testing

`test_timeout.sh` runs the utility on prepared input and compares the output
and the exit status: time limits with every suffix and fractions, invalid
durations, -s with names in either case, with SIG and as numbers, -p, the
dispositions the utility gets, the process group and the descendants (with
and without -f, also SIGKILL), stopped utilities, -k, signals passed on (and
an ignored one not), statuses 126 and 127. The binary tested is `./timeout`,
or `$U` if set.

## Files

| file | contents |
|------|----------|
| `timeout.c` | the utility |
| `test_timeout.sh` | the tests: `make test`, or `sh test_timeout.sh` |
| `Makefile` | build, test, install, clean |
