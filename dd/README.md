# dd — convert and copy a file (POSIX.1-2024)

An implementation of the `dd` utility of POSIX.1-2024 (XCU dd), in C with no
dependencies beyond libc.

    dd [operand...]

## Build and test

    make           # builds ./dd
    make test      # the tests (sh test_dd.sh)
    make difftest  # compares with GNU dd
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The operands if=, of=, ibs=, obs=, bs=, cbs=, skip=, seek=, count=,
  iflags=fullblock and conv= with ascii, ebcdic, ibm (XSI, with the tables
  of the standard), block, unblock, lcase, ucase, swab, noerror, notrunc and
  sync; sizes as numbers with k, b and x.
* The steps of the standard in order: sync padding, then (unless bs= alone
  copies block by block) swab, ascii, lcase or ucase, block or unblock,
  ebcdic or ibm, gathered into output blocks of obs bytes.
* The counts "w+p records in/out" and the truncated records at the end,
  after an input error with noerror, on SIGINT (which then ends dd) and on
  SIGUSR1 (after which it goes on).

## Choices where the standard leaves room

* Operands given more than once: the last one counts (conv= adds up).
* skip=, seek= and count= also take the size forms; count=0 copies nothing.
* iflag=fullblock (as GNU dd has it) is taken as well as iflags=fullblock.
* cbs=0 with block or unblock leaves the records as they are; ascii, ebcdic
  and ibm without cbs= just translate.
* swab works within each input block, as the standard says.
* An unfinished unblock record at the end is written like a full one.
* Skipping past the end of the input is a warning, not an error.
* seek= on output that cannot seek writes seek * obs null bytes.
* With noerror, a block that fails to read is ibs null bytes with sync (a
  partial block), or left out; dd seeks past it if it can.
* Usage errors exit with status 2; other errors with status 1.

## Testing

`test_dd.sh` runs the utility on prepared input and compares the output and
the exit status: the counts for many block sizes, bs= against ibs= and obs=
on pipes (with short reads), fullblock, skip=, seek= and the truncation of
the output, every conversion, the EBCDIC tables (round trips and known
values), sync, noerror on a read error, SIGINT, SIGUSR1 and SIGTERM, usage
errors. The binary tested is `./dd`, or `$U` if set.

`tests/dddiff.sh` compares the output and the counts with GNU dd for about
50 sets of operands on six inputs, and the output files for seek= and
notrunc.

## Files

| file | contents |
|------|----------|
| `dd.c` | the utility |
| `tests/dddiff.sh` | the comparison with GNU dd (`make difftest`) |
| `test_dd.sh` | the tests: `make test`, or `sh test_dd.sh` |
| `Makefile` | build, test, install, clean |
