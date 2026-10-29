# ps — report process status (POSIX.1-2024)

An implementation of the `ps` utility of POSIX.1-2024 (XCU ps), in C with no
dependencies beyond libc.

    ps [-aAw] [-defl] [-g grouplist] [-G grouplist] [-n namelist] [-o format]...
       [-p proclist] [-t termlist] [-u userlist] [-U userlist]

## Build and test

    make           # builds ./ps
    make test      # the tests (sh test_ps.sh)
    make difftest  # compares with procps ps
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The information from /proc (Linux); terminal names from the devices of
  /dev.
* Every selection option, -f, -l, and -o with all the names of the standard,
  headers of one's own and empty ones, columns as wide as their widest
  value.

## Choices where the standard leaves room

* The default listing PID TTY TIME CMD; -f and -l add the columns of the
  standard (-o also takes their names: f, s, uid, c, pri, addr, sz, wchan,
  stime, cmd).
* -a leaves out session leaders; -g selects by session; -G and -U take names
  too; -n is ignored.
* comm is the system's command name (15 bytes at most on Linux); args, or
  the name in square brackets; zombies are marked <defunct> after args and
  CMD.
* tty: pts/0, tty1, console, "?" for none; -t takes those, /dev/ names and
  what follows "tty".
* A field that cannot be known is "-" (nice of a real-time process); control
  characters are written as "?", format effectors as spaces.
* Lines cut at the larger of {LINE_MAX} and COLUMNS (132 with -w); not with
  -ww.

## Testing

`test_ps.sh` runs the utility on prepared input and compares the output and
the exit status: sleeping processes in sessions of their own (and a zombie):
every -o field, as the system's ps finds it too; headers; every selection
option; on a pseudo-terminal, the default selection and -t; the listings of
-f, -l and -lf; long lines; errors. The binary tested is `./ps`, or `$U` if
set.

`tests/psdiff.py` compares the -o fields that do not change for every
process, and the processes each selection option selects, with procps ps.

## Files

| file | contents |
|------|----------|
| `ps.c` | the utility |
| `tests/psdiff.py` | the comparison with procps ps (`make difftest`) |
| `test_ps.sh` | the tests: `make test`, or `sh test_ps.sh` |
| `Makefile` | build, test, install, clean |
