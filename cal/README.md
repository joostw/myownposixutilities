# cal — print a calendar (POSIX.1-2024)

An implementation of the `cal` utility of POSIX.1-2024 (XCU cal), in C with
no dependencies beyond libc. It belongs to the XSI option.

    cal [[month] year]

## Build and test

    make           # builds ./cal
    make test      # the tests (sh test_cal.sh)
    make difftest  # compares with busybox cal
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The Julian calendar up to September 2, 1752, the Gregorian one from
  September 14, 1752; a day is placed by the weekday of its Julian day
  number.
* One month, a whole year, or the current month.

## Choices where the standard leaves room

* The format of the historical cal (the same as BSD's and busybox's): the
  month and year centered over "Su Mo Tu We Th Fr Sa", days as "%2d"; a year
  as three months to a row. No trailing blanks, and only the week lines a
  month needs.
* The names of the months and weekdays are the locale's (a weekday cut to
  two columns).
* A month that is not 1 to 12 or a year that is not 1 to 9999 is an error
  (status 1); more than two operands is a usage error (status 2).

## Testing

`test_cal.sh` runs the utility on prepared input and compares the output and
the exit status: September 1752, leap years in both calendars, weekdays of
known dates, the first and the last month, the year view, the current month
(against date), errors. The binary tested is `./cal`, or `$U` if set.

`tests/caldiff.sh` compares every year from 1 to 9999 and every month of
fifteen years with busybox cal (lines without trailing blanks, empty lines
left out).

## Files

| file | contents |
|------|----------|
| `cal.c` | the utility |
| `tests/caldiff.sh` | the comparison with busybox cal (`make difftest`) |
| `test_cal.sh` | the tests: `make test`, or `sh test_cal.sh` |
| `Makefile` | build, test, install, clean |
