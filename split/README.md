# split — split a file into pieces (POSIX.1-2024)

An implementation of the `split` utility of POSIX.1-2024 (XCU split), in C
with no dependencies beyond libc.

    split [-l line_count] [-a suffix_length] [file [name]]
    split -b n[k|m] [-a suffix_length] [file [name]]

## Build and test

    make           # builds ./split
    make test      # the tests (sh test_split.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The pieces are name (x) followed by suffix_length (2) lowercase letters,
  aa, ab, ...; each is created (or truncated) as it is needed, so an empty
  input makes none.
* -l line_count lines (1000) a piece, or -b n bytes, k (1024) or m (1048576)
  times n.

## Choices where the standard leaves room

* When the suffixes run out, the last file gets its full share and split
  fails (status 1), keeping the files.
* -l 0, -b 0, -a 0 are usage errors (status 2); so are -l with -b.
* The output files are created with mode 0666, less the mask.
* A name whose last component, with the suffix, is longer than {NAME_MAX} of
  its directory is an error before any file is made.

## Testing

`test_split.sh` runs the utility on prepared input and compares the output
and the exit status: line and byte counts with k and m, the suffix length,
running out of suffixes, the name prefix, the standard input, lines longer
than a piece, the {NAME_MAX} check, comparisons with the split of the
system. The binary tested is `./split`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `split.c` | the utility |
| `test_split.sh` | the tests: `make test`, or `sh test_split.sh` |
| `Makefile` | build, test, install, clean |
