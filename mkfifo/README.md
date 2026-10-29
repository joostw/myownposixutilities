# mkfifo — make FIFO special files (POSIX.1-2024)

An implementation of the `mkfifo` utility of POSIX.1-2024 (XCU mkfifo), in C
with no dependencies beyond libc.

    mkfifo [-m mode] file...

## Build and test

    make           # builds ./mkfifo
    make test      # the tests (sh test_mkfifo.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* mkfifo() with rw for everyone, which the file mode creation mask
  restricts.
* -m: an octal or symbolic mode as for chmod (+ and - relative to a=rw),
  compiled by the same code; the FIFO is created with exactly that mode (the
  mask is 0 for the call), so it never has other permissions.

## Choices where the standard leaves room

* Without a *who*, a clause of -m leaves the bits of the mask alone, as
  chmod does.

## Testing

`test_mkfifo.sh` runs the utility on prepared input and compares the output
and the exit status: modes with and without -m against umasks, symbolic
modes, several operands, existing files, bad modes. The binary tested is
`./mkfifo`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `mkfifo.c` | the utility |
| `test_mkfifo.sh` | the tests: `make test`, or `sh test_mkfifo.sh` |
| `Makefile` | build, test, install, clean |
