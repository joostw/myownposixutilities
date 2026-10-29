# file — determine file type (POSIX.1-2024)

An implementation of the `file` utility of POSIX.1-2024 (XCU file), in C
with no dependencies beyond libc.

    file [-dh] [-M file] [-m file] file...
    file -i [-h] file...

## Build and test

    make           # builds ./file
    make test      # the tests (sh test_file.sh)
    make difftest  # compares with Christos Zoulas's file
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The file type (symbolic link with -h, directory, fifo, socket, block and
  character special); "regular file" with -i; "empty"; then the
  position-sensitive tests of -M and -m files and the defaults of -d, in the
  order of the options; then the context-sensitive defaults; "data".
* The default position-sensitive tests know ELF ("executable" for
  executables), Mach-O and PE binaries, ar, cpio (odc, newc, crc, binary)
  and tar (ustar, pax, GNU, V7 with its checksum) archives, compress, gzip,
  bzip2, xz, zstd and Zip data, PDF, PostScript, PNG, GIF, JPEG, Java
  classes and SQLite databases.
* The context-sensitive defaults tell ASCII, UTF-8 and ISO-8859 text apart,
  and in it scripts with #! ("commands text" for a shell), C ("c program
  text"), FORTRAN ("fortran program text") and shell commands without #!
  ("commands text").
* Magic files: offset (decimal, octal, hexadecimal, with > for continuation
  lines), type (d, u with sizes 1, 2, 4, 8 or C, S, I, L and a mask; s;
  byte, short, long, string), value (with =, <, >, &, ^, x; string escapes)
  and message (a printf format for the value).

## Choices where the standard leaves room

* A file that cannot be examined is "cannot open (reason)"; it does not
  change the status.
* The operand - is the standard input.
* With -m alone the defaults follow; the context-sensitive defaults come
  after all the position-sensitive tests, when -d is given or there is no
  -M.
* Fields of magic files may be separated by blanks as well as tabs; lines
  that are empty or start with # are ignored; >> lines apply when the > line
  before matched; messages are joined with a space (none for one starting
  with \b).
* Numbers in magic files are in the byte order of the machine.
* -i with -d, -m or -M is a usage error; a bad magic file is an error
  (status 1) before any operand is looked at; usage errors exit with status
  2.

## Testing

`test_file.sh` runs the utility on prepared input and compares the output
and the exit status: every file type, links with and without -h, -i, text in
three encodings, scripts, C, FORTRAN and shell, each default binary format
(made with printf and dd), magic files with every field, type, size, mask,
comparison, escape and message format, the order of -m, -M and -d, errors.
The binary tested is `./file`, or `$U` if set.

`tests/filediff.sh` puts about 2000 files of system directories in coarse
classes (ELF executables and shared objects, text, archives, compressed
data, images, ...) by the output of both, which must agree.

## Files

| file | contents |
|------|----------|
| `file.c` | the utility |
| `tests/filediff.sh` | the comparison with Christos Zoulas's file (`make difftest`) |
| `test_file.sh` | the tests: `make test`, or `sh test_file.sh` |
| `Makefile` | build, test, install, clean |
