# crontab — schedule periodic background work (POSIX.1-2024); crond

An implementation of the `crontab` utility of POSIX.1-2024 (XCU crontab), in
C with no dependencies beyond libc; and crond, the daemon that runs the
entries (POSIX does not specify one).

    crontab [file]
    crontab [-e|-l|-r]
    crond [-fs] [-T [[CC]YY]MMDDhhmm]

## Build and test

    make           # builds ./crontab and ./crond
    make test      # the tests (sh test_crontab.sh)
    make install   # into $(PREFIX)/bin, the daemon into $(PREFIX)/sbin
                   # (PREFIX=/usr/local, DESTDIR= for staging)
    make clean

## What is implemented

* Entries checked line by line (six fields, patterns of *, numbers, ranges
  and lists in their ranges) and installed whole; -e with EDITOR (vi), -l,
  -r; cron.allow and cron.deny.
* crond runs at every minute the commands whose time matches, with the
  standard's rule for the day of the month and the day of the week, as "sh
  -c" with the user's IDs, in a session of its own, the environment HOME,
  LOGNAME, USER, PATH (getconf PATH) and SHELL (its sh); the lines after the
  first % as the standard input (\% a %); the output mailed (mailx -s
  SUBJECT USER). crond -s runs one minute (-T: which) and exits.

## Choices where the standard leaves room

* The spool is /var/spool/posixcron, a file per user, and cron.allow and
  cron.deny are in /etc (CFLAGS=-DCRON_SPOOL=... -DCRON_CONFDIR=...); a
  program that is not set-user-ID takes CRON_SPOOL and CRON_CONFDIR from the
  environment.
* A line that is not valid is reported with its number, and nothing is
  installed. The day of the week is 0 to 6.
* -e: the editor is run by sh as "$EDITOR file" on a copy; when it fails, or
  the copy is not valid, nothing is installed and the copy is kept; an
  unchanged copy is not installed.
* Backslashes other than before % are left for the shell.
* -l and -r without an entry: status 1. Usage errors: status 2.

## Installing

make install puts crontab into $(PREFIX)/bin and crond into $(PREFIX)/sbin.
For users other than root, crontab must be set-user-ID root (chmod 4755) and
the spool directory root's, mode 700; crond runs as root, started at boot.

## Testing

`test_crontab.sh` runs the utility on prepared input and compares the output
and the exit status: a spool, configuration directory and mailx of the
tests' own: installing, listing, replacing, removing, valid and invalid
patterns with the line number, -e with editors that change, keep, fail and
break the entry; crond -s -T at chosen minutes for every rule of the days,
lists, the environment and directory, the mail, standard error, %, \%, the
session; cron.allow and cron.deny (without privileges). The binary tested is
`./crontab`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `crontab.c` | the utility |
| `crond.c` | the daemon |
| `cron.c` | the spool and the entries, shared |
| `cron.h` | their interface |
| `test_crontab.sh` | the tests: `make test`, or `sh test_crontab.sh` |
| `Makefile` | build, test, install, clean |
