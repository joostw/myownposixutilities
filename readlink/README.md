# readlink — display the contents of a symbolic link (POSIX.1-2024)

An implementation of the `readlink` utility of POSIX.1-2024 (XCU readlink),
in C with no dependencies beyond libc.

    readlink [-n] file

## Build and test

    make           # builds ./readlink
    make test      # the tests (sh test_readlink.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* readlink() into a buffer that grows until the contents fit; -n leaves out
  the newline. A file that is not a symbolic link is an error.

## Testing

`test_readlink.sh` runs the utility on prepared input and compares the
output and the exit status: links with spaces, dangling links, links to
links (one step), a link of 4000 bytes, files that are not links, missing
files, write errors. The binary tested is `./readlink`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `readlink.c` | the utility |
| `test_readlink.sh` | the tests: `make test`, or `sh test_readlink.sh` |
| `Makefile` | build, test, install, clean |
