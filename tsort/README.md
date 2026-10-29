# tsort — topological sort (POSIX.1-2024)

An implementation of the `tsort` utility of POSIX.1-2024 (XCU tsort), in C
with no dependencies beyond libc.

    tsort [-w] [file]

## Build and test

    make           # builds ./tsort
    make test      # the tests (sh test_tsort.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Kahn's algorithm; among the items free at a step, the one that became free
  first (in input order at the start). A cycle is found by following
  predecessors from the earliest remaining item, reported with its items,
  and broken.

## Choices where the standard leaves room

* Without -w, cycle messages are warnings and the exit status stays 0 (GNU
  tsort exits 1); with -w the status is the number of cycles, at most 125.
* An odd number of items is an error.

## Testing

`test_tsort.sh` runs the utility on prepared input and compares the output
and the exit status: chains, diamonds, pairs of the same item, separators,
cycles with and without -w, an odd number of items, a chain of 20000 items.
The binary tested is `./tsort`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `tsort.c` | the utility |
| `test_tsort.sh` | the tests: `make test`, or `sh test_tsort.sh` |
| `Makefile` | build, test, install, clean |
