# pr — print files (POSIX.1-2024)

An implementation of the `pr` utility of POSIX.1-2024 (XCU pr), in C with no
dependencies beyond libc.

    pr [+page] [-column] [-adfFmprt] [-e[char][gap]] [-h header]
       [-i[char][gap]] [-l lines] [-n[char][width]] [-o offset] [-s[char]]
       [-w width] [file...]

## Build and test

    make           # builds ./pr
    make test      # the tests (sh test_pr.sh)
    make difftest  # compares with GNU pr
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Pages of 66 lines with the header "date name Page n" and the trailer
  (blank lines, or a form feed with -f and -F); columns down (-column) or
  across (-a); -m for the files side by side; numbering (-n), tab expansion
  (-e) and making (-i), offsets (-o), separators (-s), double spacing (-d),
  +page, -r and -t.
* -p waits for a newline on /dev/tty before each page when the output is a
  terminal (-f and -p are XSI).

## Choices where the standard leaves room

* The date in the header is "%b %e %H:%M %Y" of the file's modification time
  (the current time for the standard input).
* Column widths are (width - gaps - number fields) / columns, as GNU pr has
  them; on the last page the lines are shared out among the columns, the
  first ones taking one more.
* Input tabs are expanded at the column of the output they reach.
* Usage errors exit with status 2; other errors with status 1.

## Testing

`test_pr.sh` runs the utility on prepared input and compares the output and
the exit status: headers and trailers, page lengths, every option alone and
with others, columns down and across, merging, numbering, tabs in and out,
offsets, the first page, errors. The binary tested is `./pr`, or `$U` if
set.

`tests/prdiff.sh` compares about 200 combinations of options on several
inputs with GNU pr (POSIXLY_CORRECT set), the headers made the same and tabs
expanded.

## Files

| file | contents |
|------|----------|
| `pr.c` | the utility |
| `tests/prdiff.sh` | the comparison with GNU pr (`make difftest`) |
| `test_pr.sh` | the tests: `make test`, or `sh test_pr.sh` |
| `Makefile` | build, test, install, clean |
