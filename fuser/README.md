# fuser — list process IDs of all processes that have one or more files open (POSIX.1-2024)

An implementation of the `fuser` utility of POSIX.1-2024 (XCU fuser), in C
with no dependencies beyond libc. It belongs to the XSI option.

    fuser [-cfu] file...

## Build and test

    make           # builds ./fuser
    make test      # the tests (sh test_fuser.sh)
    make difftest  # compares with psmisc fuser
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* From /proc: the working directory (c), root directory (r), program (e),
  mappings (m) and open file descriptors (no character) of each process,
  matched by device and i-node; with -c, or for a mounted block device
  without -f, by file system.
* The output of the standard, the process IDs on the standard output and the
  rest on the standard error, interleaved.

## Choices where the standard leaves room

* The use characters r, c, e, m in that order, none for an open file, as
  psmisc fuser writes them; a program is e only.
* -c with an operand that is not a mount point: its file system.
* Processes whose /proc entries cannot be read are not listed; no process is
  not an error. A file that does not exist: a diagnostic on its line, status
  1.

## Testing

`test_fuser.sh` runs the utility on prepared input and compares the output
and the exit status: processes started for the tests that use files as open
files, working directories, programs and mappings; -u; the root directory;
-c; a mounted block device when there is one; the line format with both
outputs to one file. The binary tested is `./fuser`, or `$U` if set.

`tests/fuserdiff.py` compares, for up to 150 files the running processes
use, each process ID and its use characters with psmisc fuser (with no
option, -u, and -c for directories).

## Files

| file | contents |
|------|----------|
| `fuser.c` | the utility |
| `tests/fuserdiff.py` | the comparison with psmisc fuser (`make difftest`) |
| `test_fuser.sh` | the tests: `make test`, or `sh test_fuser.sh` |
| `Makefile` | build, test, install, clean |
