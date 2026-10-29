# ls — list directory contents (POSIX.1-2024)

An implementation of the `ls` utility of POSIX.1-2024 (XCU ls), in C with no
dependencies beyond libc.

    ls [-ikqrs] [-glno] [-A|-a] [-C|-m|-x|-1] [-F|-p] [-H|-L] [-R|-d] [-S|-f|-t] [-c|-u] [file...]

## Build and test

    make           # builds ./ls
    make test      # the tests (sh test_ls.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* All the options of the synopsis, the XSI ones (-g, -o) included.
* Of each mutually-exclusive set the last option given decides; -1 does not
  turn long format off; the sort follows the last of -S and -t, where -c and
  -u count as -t once -t is given; -f ignores -r, -S and -t.
* With no operand ls acts as if given `.` (so `ls -d` writes `.`).
* Long format as the standard specifies: the date with the time for the last
  six months, with the year otherwise or in the future; link targets with
  -F/-p indicators; `total` lines in 512-byte or (-k) 1024-byte units.
* Columns (-C, -x) and streams (-m) fit COLUMNS or the terminal width, using
  the display width of multibyte names.
* Loops are detected with -R, also when -L follows symbolic links.

## Choices where the standard leaves room

* To a terminal the default format is -C; otherwise -1.
* A name containing a newline is reported on standard error (and the exit
  status is nonzero) unless -q replaces it, as the standard encourages.

## Testing

`test_ls.sh` runs the utility on prepared input and compares the output and
the exit status: every option and the rules between them, long format fields
and dates, symbolic links (dangling ones too), columns, -R and loops, UTF-8
names, errors. The binary tested is `./ls`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `ls.c` | the utility |
| `test_ls.sh` | the tests: `make test`, or `sh test_ls.sh` |
| `Makefile` | build, test, install, clean |
