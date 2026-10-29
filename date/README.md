# date — write the date and time (POSIX.1-2024)

An implementation of the `date` utility of POSIX.1-2024 (XCU date), in C
with no dependencies beyond libc.

    date [-u] [+format]
    date [-u] mmddhhmm[[cc]yy]

## Build and test

    make           # builds ./date
    make test      # the tests (sh test_date.sh)
    make difftest  # compares with GNU date
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The format through strftime() with localtime(time(0)) (-u sets TZ to
  UTC0); the default "%a %b %e %H:%M:%S %Z %Y".
* %C, %F, %G and %Y with the flags 0 and + and a minimum field width as
  POSIX.1-2024 describes them are converted by date itself (strftime() of
  glibc does not know +).
* The second form sets the clock (clock_settime()) in the timezone of TZ, or
  UTC with -u, the year from cc and yy (69 to 99 are 19yy, 00 to 68 20yy) or
  the current one.

## Choices where the standard leaves room

* The default format is the same in every locale (with its names).
* After the clock is set, the new date is written.
* A time that does not exist (a day the month does not have, an hour skipped
  for daylight saving time) is an error.
* A % at the end of the format is written as it is.
* An operand that is neither a format nor a date, and a date that is not
  valid, are errors (status 1); usage errors have status 2.

## Testing

`test_date.sh` runs the utility on prepared input and compares the output
and the exit status: every conversion (also with E and O) in five timezones,
compared with Python's time.strftime() for the second date wrote with %s;
-u; the default format; the flags and widths; empty, literal and long
formats; dates that are not valid; without privileges, the clock not set.
The tests never set the clock. The binary tested is `./date`, or `$U` if
set.

`tests/datediff.sh` compares about 70 formats (every conversion, the flags
and widths) in six timezones, with and without -u, and the default output,
with GNU date, running both until they format the same second.

## Files

| file | contents |
|------|----------|
| `date.c` | the utility |
| `tests/datediff.sh` | the comparison with GNU date (`make difftest`) |
| `test_date.sh` | the tests: `make test`, or `sh test_date.sh` |
| `Makefile` | build, test, install, clean |
