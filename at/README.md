# at, batch — execute commands at a later time (POSIX.1-2024); atd

Implementations of the `at` and `batch` utilities of POSIX.1-2024 (XCU at,
batch), one program, in C with no dependencies beyond libc; and atd, the
daemon that runs the jobs (POSIX does not specify one).

    at [-m] [-f file] [-q queuename] -t time_arg
    at [-m] [-f file] [-q queuename] timespec...
    at -r at_job_id...
    at -l -q queuename
    at -l [at_job_id...]
    batch
    atd [-fs]

## Build and test

    make           # builds ./at, ./batch (a link) and ./atd
    make test      # the tests (sh test_at.sh)
    make install   # into $(PREFIX)/bin, the daemon into $(PREFIX)/sbin
                   # (PREFIX=/usr/local, DESTDIR= for staging)
    make clean

## What is implemented

* The whole timespec grammar of the standard (times, am/pm, noon, midnight,
  now, month names, weekdays, today, tomorrow, increments and next, utc),
  and -t in the format of touch -t.
* A job is a file of the spool, written under a temporary name and renamed:
  a shell script that sets the environment, the working directory and the
  mask, then the commands. -l, -r, -q, -m and -f; batch as at -q b -m now.
* at.allow and at.deny.
* atd runs each job whose time has come, in a session of its own, with the
  user's IDs, and mails what it writes (mailx -s SUBJECT USER), always with
  -m; batch jobs one at a time. atd -s runs what is due once and exits; -f
  stays in the foreground.

## Choices where the standard leaves room

* The spool is /var/spool/posixat and at.allow and at.deny are in /etc (make
  CFLAGS=-DAT_SPOOL=... -DAT_CONFDIR=... changes them); a program that is
  not set-user-ID takes the environment variables AT_SPOOL and AT_CONFDIR.
* A job ID is "<number>.<queue>"; queues are a to z, b for batch; atd runs
  the others as a.
* Time zones: utc, gmt and z. A date of the past is not an error (the job
  runs at once); a weekday is the next one, today if the time is to come.
* The job's environment: the variables at found, but TERM, TERMCAP, DISPLAY,
  COLUMNS, LINES, SHLVL, PWD, OLDPWD and _. SHELL, if set, is the
  interpreter.
* -l lists the user's jobs (root: all), by time; -r removes the user's
  (root: any).
* at.allow and at.deny apply to every use. Errors: status 1; usage errors:
  2.

## Installing

make install puts at and batch into $(PREFIX)/bin and atd into
$(PREFIX)/sbin. For users other than root, at must be set-user-ID root
(chmod 4755) and the spool directory a directory of root's, mode 700; atd
runs as root, started at boot.

## Testing

`test_at.sh` runs the utility on prepared input and compares the output and
the exit status: a spool, configuration directory and mailx of the tests'
own: the job IDs and the messages, every form of the timespec grammar with
absolute dates and with dates relative to now (computed by Python), times
that are not valid, -l, -r, -q, batch, and, run by atd -s, the environment,
the directory, the mask, the mail of the output and of -m, jobs not yet due,
the session, -f, SHELL, batch jobs one after the other; at.allow and at.deny
(without privileges). The binary tested is `./at`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `at.c` | at and batch (it acts by its name) |
| `atd.c` | the daemon |
| `spool.c` | the spool and the job files, shared |
| `spool.h` | their interface |
| `test_at.sh` | the tests: `make test`, or `sh test_at.sh` |
| `Makefile` | build, test, install, clean |
