# diff — compare two files (POSIX.1-2024)

An implementation of the `diff` utility of POSIX.1-2024 (XCU diff), in C
with no dependencies beyond libc.

    diff [-c|-e|-f|-u|-C n|-U n] [-br] file1 file2

## Build and test

    make           # builds ./diff
    make test      # the tests (sh test_diff.sh)
    make difftest  # compares with GNU diff, patch and ed
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* Myers' O(ND) algorithm in linear space on the lines numbered by
  equivalence class (with -b after white space is normalized): a minimal
  edit script, except that, as in GNU diff, the search is cut short for very
  large inputs with very many differences.
* The default, -e, -f, -c/-C and -u/-U formats of the standard; -e scripts
  handle lines that are a single dot (with s/.//).
* Directories: Only in, Common subdirectories, the "diff OPTIONS F1 F2"
  lines, files that are not compared, -r, and loops through symbolic links.
* A file and a directory: the file of the same name in the directory.

## Choices where the standard leaves room

* A file with a NUL byte is binary: "Files F1 and F2 differ".
* A missing newline at the end makes the line differ (not with -b) and is
  marked "\ No newline at end of file".
* -c headers: name and time separated by a space, the time as date "+%a %b
  %e %T %Y"; -u times with nine digits of fraction.
* In -c, a half of a hunk lists its lines only if it has changes; in -u an
  empty context line is a single space.
* -C 0 is an error (n must be positive); -U 0 is not.

## Testing

`test_diff.sh` runs the utility on prepared input and compares the output
and the exit status: every format on additions, deletions and changes at the
start, middle and end, empty files, missing newlines, -b, binary files,
standard input, directories with -r; -e scripts are given to ed and must
make the second file. The expected output agrees with GNU diff except in the
header times and the choices above. The binary tested is `./diff`, or `$U`
if set.

`tests/rdiff.py` makes random pairs of files and checks that the edit script
is no longer than GNU diff's, that the default and -u output applied with
patch, and the -e output applied with ed, make the second file (thousands of
cases, all passing; the output is byte for byte that of GNU diff in about
two thirds of them, the rest place equally short changes elsewhere).

## Files

| file | contents |
|------|----------|
| `diff.c` | the utility |
| `tests/rdiff.py` | the random comparison (`make difftest`) |
| `test_diff.sh` | the tests: `make test`, or `sh test_diff.sh` |
| `Makefile` | build, test, install, clean |
