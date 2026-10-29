# ipcrm — remove an XSI message queue, semaphore set, or shared memory segment identifier (POSIX.1-2024)

An implementation of the `ipcrm` utility of POSIX.1-2024 (XCU ipcrm), in C
with no dependencies beyond libc. It belongs to the XSI option.

    ipcrm [-q msgid|-Q msgkey|-s semid|-S semkey|-m shmid|-M shmkey]...

## Build and test

    make           # builds ./ipcrm
    make test      # the tests (sh test_ipcrm.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* IPC_RMID with msgctl(), semctl() or shmctl() for each option in order;
  keys found with msgget(), semget() or shmget().

## Choices where the standard leaves room

* A key is an integer in C notation (decimal, 0x, 0); the key IPC_PRIVATE is
  an error.
* Each option is done even after one that fails (status 1 then). Usage
  errors (status 2) remove nothing.

## Testing

`test_ipcrm.sh` runs the utility on prepared input and compares the output
and the exit status: objects made by `testlib/ipc.c`: each option, keys in
three notations, several at once, identifiers and keys that are not there,
the wrong kind, an attached segment (removed after the detach), usage errors
that remove nothing, another user's queue. The binary tested is `./ipcrm`,
or `$U` if set.

## Files

| file | contents |
|------|----------|
| `ipcrm.c` | the utility |
| `test_ipcrm.sh` | the tests: `make test`, or `sh test_ipcrm.sh` |
| `Makefile` | build, test, install, clean |
