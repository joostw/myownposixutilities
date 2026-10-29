# ipcs — report XSI interprocess communication facilities status (POSIX.1-2024)

An implementation of the `ipcs` utility of POSIX.1-2024 (XCU ipcs), in C
with no dependencies beyond libc. It belongs to the XSI option.

    ipcs [-qms] [-a|-bcopt]

## Build and test

    make           # builds ./ipcs
    make test      # the tests (sh test_ipcs.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The three reports with the columns of the standard and of -b, -c, -o, -p
  and -t (and -a), from /proc/sysvipc, which lists every entry whatever its
  permissions; QBYTES from msgctl(IPC_STAT).
* The introductory line names /proc/sysvipc and has the date as date writes
  it.

## Choices where the standard leaves room

* Numbers right-aligned in columns at least as wide as their headings; times
  in 9 columns (" no-entry"); a heading line before a report whose columns
  differ from those of the report before.
* MODE: the first two characters are "-" (Linux does not tell who waits, nor
  clears segments at the first attach), then the permissions with w (r, w)
  or a (semaphores), and a space (no other access control).
* QBYTES of a queue without read permission: the system's default
  (/proc/sys/kernel/msgmnb).
* A facility without its /proc file is "not in system".

## Testing

`test_ipcs.sh` runs the utility on prepared input and compares the output
and the exit status: queues, segments and semaphore sets made by
`testlib/ipc.c` (and removed at the end): every column of every option, the
headings, attached segments, the times, the modes, IPC_PRIVATE. The binary
tested is `./ipcs`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `ipcs.c` | the utility |
| `test_ipcs.sh` | the tests: `make test`, or `sh test_ipcs.sh` |
| `Makefile` | build, test, install, clean |
