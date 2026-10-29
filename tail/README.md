# tail — copy the last part of a file (POSIX.1-2024)

An implementation of the `tail` utility of POSIX.1-2024 (XCU tail), in C
with no dependencies beyond libc.

    tail [-f] [-c number|-n number] [file]
    tail -r [-n number] [file]

## Build and test

    make           # builds ./tail
    make test      # the tests (sh test_tail.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Counts from the end (`-n 5`, `-5`) or from the beginning (`+5`, origin 1),
  in lines or bytes; any count, kept in buffers that grow only as needed.
* -r writes lines in reverse order (all of them by default).
* -f follows a file or FIFO operand as it grows.

## Choices where the standard leaves room

* -f is ignored when the standard input is a pipe or FIFO and there is no
  file operand.
* -r with -c or -f is a usage error.

## Testing

`test_tail.sh` runs the utility on prepared input and compares the output
and the exit status: both origins in lines and bytes, zero counts, -r, -f on
a growing file, pipes, count errors, write errors. The binary tested is
`./tail`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `tail.c` | the utility |
| `test_tail.sh` | the tests: `make test`, or `sh test_tail.sh` |
| `Makefile` | build, test, install, clean |
