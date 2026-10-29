# rm — remove directory entries (POSIX.1-2024)

An implementation of the `rm` utility of POSIX.1-2024 (XCU rm), in C with no
dependencies beyond libc.

    rm [-diRrv] file...
    rm -f [-diRrv] [file...]

## Build and test

    make           # builds ./rm
    make test      # the tests (sh test_rm.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The steps of the standard for each operand: missing files, directories
  (with -d, -r and -R), prompts, removal, -v.
* Hierarchies of any depth: everything is done with openat() and unlinkat()
  relative to open directories, so pathname length is no limit; at most 64
  directory descriptors are open (fewer under a low limit on open files),
  and a level whose descriptor was closed is reopened as ".." of the level
  below and checked by its device and inode. Symbolic links are removed,
  never followed.
* Prompts as the standard specifies: with -i; and without -f for a file that
  is not writable when the standard input is a terminal. Answers are matched
  against the locale's yesexpr.
* Operands whose last component is dot or dot-dot, and the root directory,
  are refused. -f and -i: the last one counts.

## Choices where the standard leaves room

* The prompts are "rm: remove FILE? ", "rm: remove write-protected FILE? ",
  "rm: descend into directory FILE? " and "rm: remove directory FILE? "; -v
  writes "removed FILE" or "removed directory FILE".
* An empty directory is asked about once, with -i (the standard allows
  skipping to step 2d).
* A directory is not removed, and nothing more said about it, when something
  in it was kept (by an answer or an error already reported).
* A directory that cannot be read is still removed if it is empty.

## Testing

`test_rm.sh` runs the utility on prepared input and compares the output and
the exit status: files, directories with -d and -r, symbolic links (not
followed), dot, dot-dot and the root directory (only with -d, so that a
mistake cannot remove anything), every kind of prompt and answer, -v, write
errors, a tree deeper than {PATH_MAX}, and 200 levels under a limit of 16
open files; and, when not run as root, files and directories without
permissions. The binary tested is `./rm`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `rm.c` | the utility |
| `test_rm.sh` | the tests: `make test`, or `sh test_rm.sh` |
| `Makefile` | build, test, install, clean |
