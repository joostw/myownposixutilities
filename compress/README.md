# compress, uncompress, zcat — compress and expand data (POSIX.1-2024)

An implementation of the `compress`, `uncompress` and `zcat` utilities of
POSIX.1-2024 (XCU compress, uncompress, zcat), one program, in C with no
dependencies beyond libc (LZW and DEFLATE are its own). They belong to the
XSI option.

    compress [-fv] [-b value] [-g | -m algo] [file...]
    compress -c [-fv] [-b value] [-g | -m algo] [file]
    compress -d [-cfv] [file...]
    uncompress [-cdfv] [file...]
    zcat [file...]

## Build and test

    make           # builds ./compress, and the links ./uncompress and ./zcat
    make test      # the tests (sh test_compress.sh)
    make difftest  # compares with gzip and Python
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The algorithms lzw (the .Z format of compress 4.0, with block mode and the
  ratio check, -b 9 to 16 bits, 16 by default) and deflate or gzip (the gzip
  format of RFC 1952, .gz, -b 1 to 9, 6 by default: hash chains, lazy
  matching, and each block stored, with fixed codes or with dynamic codes,
  whichever is shortest).
* Expanding finds the format from the data; gzip members one after another,
  every header field, and the CRC-32 and the size are checked.
* A file is replaced by name.Z (or name.gz) with the owner (when allowed),
  mode and times of the input; expanding takes the suffix off, or
  (obsolescent) adds .Z to a name without one to find the input.
* An existing output is overwritten with -f, after a question when the
  standard input is a terminal, and is an error otherwise.

## Choices where the standard leaves room

* The usage status is 1 for compress, as 2 means "not compressed", and 2 for
  uncompress and zcat; an unknown -m algo is status 3, before any file is
  touched.
* A file is not compressed (status 2, without -f) when the output would be
  larger; with -c or the standard input the data has been written by then.
* Compressing a file that already ends in the suffix, or one that is not a
  regular file, is an error, and so is a symbolic link unless -f is given.
* A file with more than one link is an error without -f; with -f that link
  is done.
* With -f, an input that cannot be removed leaves both files, with a
  warning.
* With -c, expanding a missing file that does not end in .Z tries the name
  with .Z.
* Anything after a gzip member that is not another member is an error, and
  so is an empty input.

## Testing

`test_compress.sh` runs the utility on prepared input and compares the
output and the exit status: the headers of both formats, known codes and
known gzip data (every header field, stored blocks, two members), round
trips at every size and level, compression ratios, files replaced with their
mode and times, the obsolescent names, -c, -, the status 2 cases, links,
existing outputs (also on a terminal), {NAME_MAX}, damaged input, -v, the
options. The binary tested is `./compress`, or `$U` if set.

`tests/compressdiff.py` makes about 70 inputs of many kinds: what compress
writes must be expanded by gzip -d (which reads .Z too) and by Python's
gzip, what gzip and Python write at every level must be expanded by compress
-d, and damaged input must not crash it.

## Files

| file | contents |
|------|----------|
| `compress.c` | the program: options, files, buffered input and output |
| `lzw.c` | the .Z format (LZW) |
| `gzip.c` | the gzip format: DEFLATE, inflate and CRC-32 |
| `compress.h` | what the three share |
| `tests/compressdiff.py` | the comparison with gzip and Python (`make difftest`) |
| `test_compress.sh` | the tests: `make test`, or `sh test_compress.sh` |
| `Makefile` | build, test, install, clean |
