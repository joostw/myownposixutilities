# nohup — invoke a utility immune to hangups (POSIX.1-2024)

An implementation of the `nohup` utility of POSIX.1-2024 (XCU nohup), in C
with no dependencies beyond libc.

    nohup utility [argument...]

## Build and test

    make           # builds ./nohup
    make test      # the tests (sh test_nohup.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* SIGHUP ignored; the standard output, if a terminal, appended to nohup.out
  (else $HOME/nohup.out), created with the mode rw for the owner only; the
  standard error, if a terminal, to the standard output when that is open
  and not a terminal, else to nohup.out; the message "nohup: appending
  output to FILE".
* The utility is searched for in PATH as execvp() does; a file that is not a
  program (ENOEXEC) is run by sh; the status is 127 when it is not found (a
  directory in PATH that cannot be searched does not count as finding it)
  and 126 when it is found but cannot be executed. The errors of nohup
  itself exit with 127.

## Choices where the standard leaves room

* A terminal on the standard input is replaced by /dev/null.
* An existing nohup.out keeps its mode.
* A first "--" is discarded.

## Testing

`test_nohup.sh` runs the utility on prepared input and compares the output
and the exit status: no terminal (nothing changes), SIGHUP ignored by the
utility and its children; with `testlib/ptyrun.py`, a terminal on the
standard output, on both, on the standard error only (with the standard
output a file, a pipe, or closed) and on the standard input; nohup.out
appended to, its mode whatever the mask, $HOME/nohup.out, neither possible
(127); statuses 126 and 127. The binary tested is `./nohup`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `nohup.c` | the utility |
| `test_nohup.sh` | the tests: `make test`, or `sh test_nohup.sh` |
| `Makefile` | build, test, install, clean |
