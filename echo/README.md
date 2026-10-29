# echo — write arguments to standard output (POSIX.1-2024)

An implementation of the `echo` utility of POSIX.1-2024 (XCU echo), in C
with no dependencies beyond libc.

    echo [string...]

## Build and test

    make           # builds ./echo
    make test      # the tests (sh test_echo.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The XSI behaviour: no options (`-n` is an ordinary string), and the escape
  sequences `\a \b \c \f \n \r \t \v \\ \0num` in every argument.
* `\c` ends the output without a newline.

## Testing

`test_echo.sh` runs the utility on prepared input and compares the output
and the exit status: each escape, `\0num` (at most three digits), `\c`,
unknown escapes, `-n` and `--` as operands, write errors. The binary tested
is `./echo`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `echo.c` | the utility |
| `test_echo.sh` | the tests: `make test`, or `sh test_echo.sh` |
| `Makefile` | build, test, install, clean |
