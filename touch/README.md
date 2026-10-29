# touch — change file access and modification times (POSIX.1-2024)

An implementation of the `touch` utility of POSIX.1-2024 (XCU touch), in C
with no dependencies beyond libc.

    touch [-acm] [-r ref_file|-t time|-d date_time] file...

## Build and test

    make           # builds ./touch
    make test      # the tests (sh test_touch.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* -a, -m and -c; times from -r, -t (`[[CC]YY]MMDDhhmm[.SS]`) or -d
  (`YYYY-MM-DDThh:mm:SS[.frac][Z]`, with a space for T or a comma for the
  period), with nanoseconds.
* Nonexistent files are created as creat() would, then given the times.
* -t and -d dates must exist (no 30 February); second 60 means one second
  after second 59.

## Choices where the standard leaves room

* -r, -t and -d are mutually exclusive.
* Two-digit years 69-99 are 19YY, 00-68 20YY.
* Times before the Epoch are accepted where the system can store them.

## Testing

`test_touch.sh` runs the utility on prepared input and compares the output
and the exit status: every option and time format, time zones, leap seconds,
invalid dates, creation and -c, options after operands (which are file
names), errors. The binary tested is `./touch`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `touch.c` | the utility |
| `test_touch.sh` | the tests: `make test`, or `sh test_touch.sh` |
| `Makefile` | build, test, install, clean |
