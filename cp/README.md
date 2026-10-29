# cp — copy files (POSIX.1-2024)

An implementation of the `cp` utility of POSIX.1-2024 (XCU cp), in C with no
dependencies beyond libc.

    cp [-Pfip] source_file target_file
    cp [-Pfip] source_file... target
    cp -R [-H|-L|-P] [-fip] source_file... target

## Build and test

    make           # builds ./cp
    make test      # the tests (sh test_cp.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The three forms and the steps of the standard for regular files,
  directories and (with -R) other file types: FIFOs, devices, sockets and
  symbolic links are created as such.
* -f (unlink when the target cannot be opened), -i (a prompt for an existing
  target), -p (owner and group, mode with the set-ID bits, times).
* New files and directories get the permissions of the standard, the umask
  applied; a directory is created with u+rwx so that its contents can be
  written, and given its final mode afterwards.
* Hierarchies of any depth in both trees: *at() functions relative to open
  directories, at most 32 descriptors per tree, a closed level reopened as
  ".." and checked; a source level reached through a link that -L followed
  keeps the level above it open.
* Copying a file to itself, and a directory into itself, are refused;
  directory cycles met with -L are reported and not followed.

## Choices where the standard leaves room

* -R without -H, -L or -P acts as -P.
* An existing destination that is not a directory is removed before a FIFO,
  device or symbolic link is created in its place (after a prompt, with -i),
  so that a hierarchy can be copied again over an earlier copy.
* -p sets the owner first (EPERM is not reported; the set-ID bits are then
  cleared), then the mode, then the times; directories get their times after
  their contents.
* -P without -R copies a symbolic link operand as a link.
* The prompt of -i is "cp: overwrite FILE? ".

## Testing

`test_cp.sh` runs the utility on prepared input and compares the output and
the exit status: the three forms, modes and the umask, existing targets, the
same file, symbolic links with -H, -L and -P, FIFOs, -p (times, modes,
owners as root), -i, -f, directories into themselves and -L cycles,
read-only directories, a tree deeper than {PATH_MAX} and 150 levels with 16
open files, write errors; and, not as root, unwritable targets and
unreadable sources. The binary tested is `./cp`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `cp.c` | the utility |
| `test_cp.sh` | the tests: `make test`, or `sh test_cp.sh` |
| `Makefile` | build, test, install, clean |
