# m4 — macro processor (POSIX.1-2024)

An implementation of the `m4` utility of POSIX.1-2024 (XCU m4), in C with no
dependencies beyond libc.

    m4 [-s] [-D name[=val]]... [-U name]... [file...]

## Build and test

    make           # builds ./m4
    make test      # the tests (sh test_m4.sh)
    make difftest  # compares with GNU m4
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The input is a stack of files and texts pushed back to be read again;
  tokens are comments, names, quoted strings and characters; a macro called
  with arguments when a ( follows at once, its expansion read again.
* All the built-ins of the standard: changecom, changequote, decr, define,
  defn, divert, divnum, dnl, dumpdef, errprint, eval, ifdef, ifelse,
  include, incr, index, len, m4exit, m4wrap, mkstemp, popdef, pushdef,
  shift, sinclude, substr, syscmd, sysval, traceon, traceoff, translit,
  undefine and undivert; $0 to $9, $#, $* and $@.
* syscmd gives the output of the command as its defining text, not
  rescanned, as POSIX.1-2024 has it.

## Choices where the standard leaves room

* Options may come after operands; the -D and -U are done in order before
  the first file is read. -s writes #line lines as GNU m4 does.
* The built-ins whose behaviour is unspecified without ( are then just text.
* define replaces the current definition only; defn of a built-in gives a
  token that define and pushdef take as that built-in.
* changequote with one argument: that and the default end quote; an empty
  begin quote turns quoting off. changecom with an empty argument turns
  comments off.
* Numbers are 32-bit, wrapping around; shifts count modulo 32; division by
  zero is an error.
* divert takes numbers up to 1023; undivert with an argument that is not a
  number writes that file, unscanned.
* len, index, substr and translit count bytes; translit takes ranges.
* m4exit discards the diversions and the m4wrap text; the m4wrap texts are
  read in the order they were given.
* Errors write "m4: file:line: message" and make the exit status 1; an end
  of file in a quoted string or in arguments ends m4. Usage errors exit with
  status 2.

## Testing

`test_m4.sh` runs the utility on prepared input and compares the output and
the exit status: tokens, quotes and comments, argument collection and
substitution, each built-in with its edge cases and errors, -D and -U in
order, options after operands, files and -, -s. The binary tested is `./m4`,
or `$U` if set.

`tests/m4diff.sh` compares the output and the status (and the standard error
of errprint and dumpdef) of about 55 inputs with GNU m4 -G: recursion,
quoting, diversions, eval, the classic forloop and reverse macros, -D, -U
and -s.

## Files

| file | contents |
|------|----------|
| `m4.c` | the utility |
| `tests/m4diff.sh` | the comparison with GNU m4 (`make difftest`) |
| `test_m4.sh` | the tests: `make test`, or `sh test_m4.sh` |
| `Makefile` | build, test, install, clean |
