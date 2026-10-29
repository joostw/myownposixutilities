# sccs — the Source Code Control System (POSIX.1-2024): admin, delta, get, prs, rmdel, sact, sccs, unget, val, what

Implementations of the SCCS utilities of POSIX.1-2024 (XCU admin, delta,
get, prs, rmdel, sact, sccs, unget, val and what), one program that acts by
its name, in C with no dependencies beyond libc. They belong to the XSI
option. The SCCS files have the classic format, the one of AT&T SCCS,
Schilling's SCCS and CSSC, so files can go between them.

    admin -i[name] [-n] [-a login] [-d flag] [-e login] [-f flag] [-m mrlist]
          [-r rel] [-t[name]] [-y[comment]] newfile
    admin -n [-a login] [-d flag] [-e login] [-f flag] [-m mrlist] [-t[name]]
          [-y[comment]] newfile...
    admin [-a login] [-d flag] [-m mrlist] [-r rel] [-t[name]] file...
    admin -h file...
    admin -z file...
    delta [-nps] [-g list] [-m mrlist] [-r SID] [-y[comment]] file...
    get [-begkmnlLpst] [-c cutoff] [-i list] [-r SID] [-x list] file...
    prs [-a] [-d dataspec] [-r[SID]] file...
    prs [-e|-l] -c cutoff [-d dataspec] file...
    prs [-e|-l] -r[SID] [-d dataspec] file...
    rmdel -r SID file...
    sact file...
    unget [-ns] [-r SID] file...
    val -
    val [-s] [-m name] [-r SID] [-y type] file...
    what [-s] file...
    sccs [-r] [-d path] [-p path] command [options...] [operands...]

## Build and test

    make           # builds ./sccs, and the links ./admin, ./delta, ./get, ./prs, ./rmdel, ./sact, ./unget, ./val and ./what
    make test      # the tests (sh test_sccs.sh)
    make difftest  # compares with CSSC
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The SCCS file: the delta table, the users, the flags, the descriptive text
  and the weave of ^AI, ^AD and ^AE blocks, with its checksum; new versions
  written to the x-file and renamed, under the lock of the z-file; the
  p-file of get -e.
* get: the table of the standard for the SID got and the SID of the new
  delta (with -b and the b flag, -t, and the d flag), -i and -x lists with
  ranges, -c cutoffs, every id keyword, -k, -m, -n, -p, -s, -g, the l-file
  and -L; the floor, ceiling, locked releases, joint edits and the list of
  users for get -e.
* delta: the differences (a shortest edit script) put into the weave, the
  statistics, comments and MRs (with the v flag and its validation program),
  null deltas for the n flag, -g, -n, -p, -r, -s.
* admin: new files with -i, -n, -r, -t, -y, -m; flags set and cleared (all
  of the standard's: b, c, f, d, i, j, l, n, q, m, t, v), users added and
  erased, -h and -z.
* prs: every data keyword of the standard, -r, -e, -l, -c and -a; rmdel,
  sact, unget, val (with its exit bits and the - form), what.
* sccs: the SCCS/s. names with -d, -p and PROJECTDIR, the utilities, and
  check, clean, create, delget, deledit, diffs, edit, fix, info, print, tell
  and unedit.
* Directories and - as operands (the s. files in a directory, names from the
  standard input) for every utility that takes them.

## Choices where the standard leaves room

* One program; sccs runs the utilities in a child process.
* Dates have two-digit years (69 to 99 are 19yy) in the time of TZ. New SCCS
  files have mode 0444; the g-file 0444 with keywords replaced, else 0644,
  less the mask.
* A wrong checksum makes the file corrupted (an error), but for admin -h,
  which reports it, and admin -z, which writes a new one.
* admin: -m, -r and -y only with a new file, -r only with -i; -n alone makes
  delta 1.1 with no lines.
* A list of users that is not empty lets only those named (or in a group
  named) make deltas, also when it only names users with ! (as CSSC).
* get: a writable g-file in the way is an error; no id keywords is a
  warning, an error with the i flag; -t without -r is the newest delta of
  the highest release; with -c the deltas after the cutoff are not applied;
  a delta both included and excluded is included.
* get -e: the floor and ceiling apply to the release got, the locked
  releases to that of the new delta; the new SIDs in the p-file are taken,
  and a branch is made then.
* delta: without -y the comments (and MRs) come from the standard input,
  after the prompt only on a terminal; the v-flag program is run as "program
  module type MR..."; new lines go into the weave where CSSC puts them.
* prs: -d without -r, -e, -l or -c is for the newest delta; :PN: is the name
  as given; unset flags are "none" or "no"; :GB: has its keywords replaced.
* rmdel: only a delta with no successor on any branch; it stays as type R.
* sccs: operands whose last component starts with s. are used as they are,
  no operands is the SCCS directory; create renames to ,name and gets the
  files; -b of info, check, tell and clean leaves out edits that make branch
  deltas.
* Usage errors exit with status 2 and other errors with status 1 (val and
  what have their own). main.c lists the rest.

## Testing

`test_sccs.sh` runs the utility on prepared input and compares the output
and the exit status: for each utility its options and cases: admin's new
files, flags, users, -h and -z; get's SID table, lists, cutoffs, keywords,
l-file, edits and their checks; delta's report, comments (also typed on a
terminal), MRs, null deltas and twelve versions each got back; every data
keyword of prs; rmdel, sact, unget; every exit bit of val; what; the
pseudo-utilities of sccs and its names. The binary tested is `./sccs`, or
`$U` if set.

`tests/csscdiff.py` makes random histories twice, with these utilities and
with CSSC (admin, edits with every kind of -r and -b, deltas, rmdel, unget,
flags and users, get -p with -m, -n, -k, -i, -x, -t, -c and -l, prs with
every data keyword, sact, val), and compares the output, the status, the g-,
p- and l-files, the SCCS files up to the body, and every version (by both
programs from both files); then the front end sccs against CSSC's.

## Files

| file | contents |
|------|----------|
| `main.c` | the program: which utility, by its name; the choices made |
| `sccs.h` | what the parts share |
| `sfile.c` | the SCCS file, SIDs, the deltas applied, the weave, the p-file, locks, keywords |
| `ldiff.c` | the differences between two lists of lines |
| `admin.c, delta.c, get.c, prs.c, rmdel.c, sact.c, unget.c, val.c, what.c` | the utilities |
| `front.c` | sccs, the front end |
| `tests/csscdiff.py` | the comparison with CSSC (`make difftest`) |
| `test_sccs.sh` | the tests: `make test`, or `sh test_sccs.sh` |
| `Makefile` | build, test, install, clean |
