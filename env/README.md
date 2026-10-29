# env — set the environment for command invocation (POSIX.1-2024)

An implementation of the `env` utility of POSIX.1-2024 (XCU env), in C with
no dependencies beyond libc.

    env [-i] [name=value]... [utility [argument...]]

## Build and test

    make           # builds ./env
    make test      # the tests (sh test_env.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* -i, name=value operands, writing the environment, and running the utility
  with the exit statuses of the standard (126: found but not run, 127: not
  found).
* The options are read by hand: scanning stops at the first operand, so the
  utility's own options are its own.
* The utility is searched for in the PATH of the new environment (getconf
  PATH when it has none); a file that is not a program (ENOEXEC) is run by
  sh. A directory in PATH that cannot be searched does not count as finding
  the utility (127, not 126).

## Choices where the standard leaves room

* A first argument "-" acts as -i.
* A name=value replaces an earlier entry of the same name.
* Errors of env itself exit with 125.

## Testing

`test_env.sh` runs the utility on prepared input and compares the output and
the exit status: -i, assignments and their order, writing the environment,
PATH from an assignment and empty PATH entries, scripts without #!, the
statuses 126 and 127, the utility's own options, write errors. The binary
tested is `./env`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `env.c` | the utility |
| `test_env.sh` | the tests: `make test`, or `sh test_env.sh` |
| `Makefile` | build, test, install, clean |
