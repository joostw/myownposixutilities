# id — return user identity (POSIX.1-2024)

An implementation of the `id` utility of POSIX.1-2024 (XCU id), in C with no
dependencies beyond libc.

    id [user]
    id -G [-n] [user]
    id -g [-nr] [user]
    id -u [-nr] [user]

## Build and test

    make           # builds ./id
    make test      # the tests (sh test_id.sh)
    make difftest  # compares with GNU id
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The default output of the standard (uid=, gid=, euid= and egid= when they
  differ, groups=), with the names left out where there are none; -G, -g,
  -u, -n and -r.
* For a user operand the IDs of its entry in the user database, and its
  groups: the group of the entry and those of the group database that list
  the user.

## Choices where the standard leaves room

* groups= is written whenever the system has supplementary groups: the
  effective group first, then the others in the order getgroups() gives
  them, each once.
* -G writes the effective group, then the real one if it differs, then the
  others.
* A user operand that is a decimal number and no login name is taken as a
  user ID.
* -n for an ID without a name writes the number.
* -n or -r alone, -r with -G, and two of -G, -g and -u are usage errors
  (status 2).

## Testing

`test_id.sh` runs the utility on prepared input and compares the output and
the exit status: every option against the system's id, a user operand and a
user ID, unknown users, usage errors; as root, a different effective user
and group and IDs without names (set with Python), -r. The binary tested is
`./id`, or `$U` if set.

`tests/iddiff.sh` compares -u, -un, -g, -gn, -ur, -gr, -G and -Gn (as sets)
and the default output with GNU id for every user of the user database and
for the process.

## Files

| file | contents |
|------|----------|
| `id.c` | the utility |
| `tests/iddiff.sh` | the comparison with GNU id (`make difftest`) |
| `test_id.sh` | the tests: `make test`, or `sh test_id.sh` |
| `Makefile` | build, test, install, clean |
