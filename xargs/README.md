# xargs — construct argument lists and invoke utility (POSIX.1-2024)

An implementation of the `xargs` utility of POSIX.1-2024 (XCU xargs), in C
with no dependencies beyond libc.

    xargs [-prtx] [-E eofstr|-0] [-I replstr|-L number|-n number]
          [-s size] [utility [argument...]]

## Build and test

    make           # builds ./xargs
    make test      # the tests (sh test_xargs.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Quoting and escapes in the input, -E, -0, -I (five or more arguments with
  replstr), -L, -n, -s, -x, -r, -t, -p (answers from /dev/tty).
* The command line length as the standard counts it, below the -s size and
  below {ARG_MAX} - 2048 less the environment.
* The options are read by hand: the utility's own options are its own.
* Exit statuses: 126 and 127 as the standard says (a directory in PATH that
  cannot be searched does not count as finding the utility); an invocation
  that exits with 255 stops xargs.

## Choices where the standard leaves room

* Without -E there is no logical end-of-file string.
* -L as POSIX.1-2024 describes it: each line is one argument (quotes
  processed, blanks kept), a trailing blank continues it on the next
  non-empty line. Historical implementations, GNU xargs among them, split
  such lines at blanks.
* The default size is 128 KiB (or what {ARG_MAX} allows); -I, -L and -n: the
  last one given counts; with -I, empty lines are skipped.
* The utility's standard input is /dev/null.
* Statuses: 123 if an invocation fails, 124 on 255, 125 if one is killed, 1
  for xargs's own errors (an unmatched quote is reported after the arguments
  before it are used).
* -0: bytes after the last null byte are ignored, with a warning.

## Testing

`test_xargs.sh` runs the utility on prepared input and compares the output
and the exit status: quoting, -n and -s and their interplay with -x, many
arguments over several commands, -E, -0, -I, -L, -t, -r, the exit statuses,
and the separation of the utility's input and options. The binary tested is
`./xargs`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `xargs.c` | the utility |
| `test_xargs.sh` | the tests: `make test`, or `sh test_xargs.sh` |
| `Makefile` | build, test, install, clean |
