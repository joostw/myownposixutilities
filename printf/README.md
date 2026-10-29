# printf — write formatted output (POSIX.1-2024)

An implementation of the `printf` utility of POSIX.1-2024 (XCU printf), in C
with no dependencies beyond libc.

    printf format [argument...]

## Build and test

    make           # builds ./printf
    make test      # the tests (sh test_printf.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The escapes of the format, %b with \0ddd and \c, the conversions d i o u x
  X c s and the floating ones a A e E f F g G (in long double).
* Numbered conversions (%n$), and their meaning when the format is reused.
* Arguments as C integer constants (octal, hexadecimal, a sign), character
  constants ('x, also for multibyte characters), and the diagnostics for
  arguments that are not completely converted, with the value so far
  written.
* Output through stdio; a write error makes the exit status 1.

## Choices where the standard leaves room

* A first "--" is discarded.
* * is accepted for width and precision (*m$ with numbered conversions); h l
  L q j z t are accepted and ignored.
* In the format, \" and \' are the quote characters (as in the sh built-in);
  a backslash before another character is written with it.
* %c of an empty argument writes nothing.
* An invalid conversion is reported and nothing more is written (status 1);
  a missing numbered argument is reported (status 1) and taken as empty or
  zero.

## Testing

`test_printf.sh` runs the utility on prepared input and compares the output
and the exit status: escapes, every conversion with flags, width and
precision, character constants (also in UTF-8), the reuse of the format,
numbered arguments, %b, conversion errors and write errors; the output is
checked byte for byte with od. The results agree with GNU printf except
where noted above, and for numbered arguments, which GNU printf does not
have. The binary tested is `./printf`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `printf.c` | the utility |
| `test_printf.sh` | the tests: `make test`, or `sh test_printf.sh` |
| `Makefile` | build, test, install, clean |
