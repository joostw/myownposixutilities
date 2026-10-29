# mv — move files (POSIX.1-2024)

An implementation of the `mv` utility of POSIX.1-2024 (XCU mv), in C with no
dependencies beyond libc.

    mv [-if] source_file target_file
    mv [-if] source_file... target_dir

## Build and test

    make           # builds ./mv
    make test      # the tests (sh test_mv.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The steps of the standard: the prompt (with -i, or for a write-protected
  target when the input is a terminal), the same-file rules, rename().
* Across file systems (EXDEV) the hierarchy is duplicated, with symbolic
  links as links and the times, owner, group and mode of every file, and the
  source is removed only if all of it was duplicated. Both walks use *at()
  functions relative to open directories: no limit on depth or pathname
  length.
* A destination of the other kind (directory or not) is an error; an
  existing one is removed first when copying.

## Choices where the standard leaves room

* The same directory entry is a diagnosed error (status 1); hard links to
  the same file: the source name is removed.
* Across file systems, hard links become separate files.
* Failures to set a copy's owner, mode or times are reported and do not
  change the exit status, as the standard says; without the owner, the
  set-ID bits are cleared.
* The prompt is "mv: overwrite FILE? " (or "... write-protected FILE? ").

## Testing

`test_mv.sh` runs the utility on prepared input and compares the output and
the exit status: renames, directories, slashes, the same file and hard
links, symbolic links, -i and -f, a tree deeper than {PATH_MAX}; and, when
/dev/shm (or $MV_OTHER_FS) is another file system, moves across it: files
with their modes and times, a hierarchy with links and a FIFO, owners (as
root), existing targets of either kind, a deep tree with 16 open files, and
(not as root) a hierarchy that cannot be read completely, which is not
removed. The binary tested is `./mv`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `mv.c` | the utility |
| `test_mv.sh` | the tests: `make test`, or `sh test_mv.sh` |
| `Makefile` | build, test, install, clean |
