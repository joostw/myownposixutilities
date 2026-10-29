# newgrp — change to a new group (POSIX.1-2024)

An implementation of the `newgrp` utility of POSIX.1-2024 (XCU newgrp), in C
with no dependencies beyond libc.

    newgrp [-l] [group]

## Build and test

    make           # builds ./newgrp
    make test      # the tests (sh test_newgrp.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The real and effective group IDs set to the group (with the privileges for
  it: newgrp is installed set-user-ID root, or run by root), the
  supplementary groups changed as the standard describes for either kind of
  system, the privileges given up, and the user's login shell executed. No
  operand: back to the user's own groups.
* The supplementary groups are set with setgroups(), which POSIX does not
  specify.

## Choices where the standard leaves room

* Group passwords are not used (POSIX has no interface to them): a user who
  is not root may change only to a group it is a member of.
* When the group cannot be assigned, a diagnostic, and the shell all the
  same, with the groups unchanged (as the standard requires).
* The shell is the login shell of the user's entry. -l (and a first "-"): a
  login shell, the environment of a new login (HOME, SHELL, USER, LOGNAME,
  PATH of getconf PATH, TERM kept), the home directory.

## Installing

For users other than root, newgrp has to be installed set-user-ID root
(chmod 4755, owned by root); make install does not do that.

## Testing

`test_newgrp.sh` runs the utility on prepared input and compares the output
and the exit status: (as root) the group IDs and the supplementary groups
for the four cases of the standard (set up with Python), a numeric group,
back to the user's groups, what the new shell keeps (the directory, the
mask, exported variables, not others), -l, the exit status, unknown groups;
a set-user-ID run by another user (not a member). The binary tested is
`./newgrp`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `newgrp.c` | the utility |
| `test_newgrp.sh` | the tests: `make test`, or `sh test_newgrp.sh` |
| `Makefile` | build, test, install, clean |
