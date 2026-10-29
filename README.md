# posixutilities — utilities of POSIX.1-2024

Implementations of the utilities of POSIX.1-2024 (IEEE Std 1003.1-2024,
The Open Group Base Specifications Issue 8, volume XCU), written from
the standard.  Each is in C11 with POSIX interfaces only, in its own
directory, with its own tests.  The aim is the full suite; 113 utilities
are done, in 100 directories (plus the built-ins of the shell), the rest
are listed under [Status](#status).

    make              # build all of them
    make test         # build, then run every test suite (sh run_tests.sh)
    make difftest     # compare with other implementations, where installed
    make install      # into /usr/local/bin; PREFIX= and DESTDIR= as usual
    make clean

One utility: `cd sed && make test`, or `sh run_tests.sh sed ls`.

## The utilities

| directory | utility | lines | tests |
|-----------|---------|------:|------:|
| [at](at/) | execute commands at a later time (with batch; and atd, its daemon) | 1234 | 88 |
| [awk](awk/) | pattern scanning and processing language | 5199 | 331 |
| [basename](basename/) | return non-directory portion of a pathname | 66 | 18 |
| [bc](bc/) | arbitrary-precision arithmetic language | 2986 | 12 |
| [cal](cal/) | print a calendar | 256 | 33 |
| [cat](cat/) | concatenate and print files | 100 | 13 |
| [chgrp](chgrp/) | change the file group ownership | 296 | 15 |
| [chmod](chmod/) | change the file modes | 404 | 94 |
| [chown](chown/) | change the file ownership | 317 | 24 |
| [cksum](cksum/) | write file checksums and sizes | 97 | 10 |
| [cmp](cmp/) | compare two files | 162 | 35 |
| [comm](comm/) | select or reject lines common to two files | 134 | 19 |
| [compress](compress/) | compress and expand data (with uncompress and zcat) | 1741 | 123 |
| [cp](cp/) | copy files | 700 | 77 |
| [crontab](crontab/) | schedule periodic background work (and crond, its daemon) | 981 | 71 |
| [csplit](csplit/) | split files based on context | 323 | 53 |
| [cut](cut/) | cut out selected fields of each line of a file | 400 | 39 |
| [date](date/) | write the date and time | 274 | 58 |
| [dd](dd/) | convert and copy a file | 683 | 114 |
| [diff](diff/) | compare two files | 1069 | 63 |
| [dirname](dirname/) | return the directory portion of a pathname | 66 | 16 |
| [echo](echo/) | write arguments to standard output | 73 | 12 |
| [ed](ed/) | edit text | 2198 | 99 |
| [env](env/) | set the environment for command invocation | 198 | 29 |
| [expand](expand/) | convert tabs to spaces | 195 | 18 |
| [expr](expr/) | evaluate arguments as an expression | 329 | 52 |
| [false](false/) | return false value | 13 | 2 |
| [file](file/) | determine file type | 1162 | 109 |
| [find](find/) | find files | 957 | 100 |
| [fold](fold/) | filter for folding lines | 238 | 24 |
| [fuser](fuser/) | list process IDs of all processes that have one or more files open | 265 | 28 |
| [getconf](getconf/) | get configuration values | 1126 | 40 |
| [grep](grep/) | search files for a pattern | 693 | 57 |
| [head](head/) | copy the first part of files | 146 | 16 |
| [iconv](iconv/) | codeset conversion | 659 | 47 |
| [id](id/) | return user identity | 210 | 38 |
| [ipcrm](ipcrm/) | remove an XSI message queue, semaphore set, or shared memory segment identifier | 104 | 36 |
| [ipcs](ipcs/) | report XSI interprocess communication facilities status | 350 | 40 |
| [join](join/) | relational database operator | 407 | 22 |
| [lex](lex/) | generate programs for lexical tasks (with libl.a) | 2770 | 11 |
| [link](link/) | call the link() function | 41 | 11 |
| [ln](ln/) | link files | 192 | 29 |
| [logger](logger/) | log messages | 258 | 27 |
| [logname](logname/) | return the user's login name | 37 | 7 |
| [ls](ls/) | list directory contents | 981 | 61 |
| [m4](m4/) | macro processor | 1537 | 102 |
| [mailx](mailx/) | process messages | 4820 | 27 |
| [make](make/) | maintain, update, and regenerate files | 3597 | 14 |
| [mkdir](mkdir/) | make directories | 349 | 35 |
| [mkfifo](mkfifo/) | make FIFO special files | 194 | 19 |
| [more](more/) | display files on a page-by-page basis | 1715 | 91 |
| [mv](mv/) | move files | 632 | 61 |
| [newgrp](newgrp/) | change to a new group | 251 | 26 |
| [nice](nice/) | invoke a utility with an altered nice value | 158 | 27 |
| [nl](nl/) | line numbering filter | 262 | 32 |
| [nohup](nohup/) | invoke a utility immune to hangups | 210 | 38 |
| [od](od/) | dump files in various formats | 618 | 65 |
| [paste](paste/) | merge corresponding or subsequent lines of files | 213 | 22 |
| [patch](patch/) | apply changes to files | 1572 | 70 |
| [pathchk](pathchk/) | check pathnames | 191 | 28 |
| [pax](pax/) | portable archive interchange | 3965 | 19 |
| [pr](pr/) | print files | 811 | 58 |
| [printf](printf/) | write formatted output | 462 | 71 |
| [ps](ps/) | report process status | 777 | 88 |
| [pwd](pwd/) | return working directory name | 103 | 12 |
| [readlink](readlink/) | display the contents of a symbolic link | 68 | 13 |
| [realpath](realpath/) | resolve a pathname | 187 | 22 |
| [renice](renice/) | set nice values of running processes | 173 | 26 |
| [rm](rm/) | remove directory entries | 424 | 78 |
| [rmdir](rmdir/) | remove directories | 83 | 22 |
| [sccs](sccs/) | the Source Code Control System: admin, delta, get, prs, rmdel, sact, sccs, unget, val, what | 4789 | 218 |
| [sed](sed/) | stream editor | 1396 | 123 |
| [sh](sh/) | shell, the standard command language interpreter | 11967 | 406 |
| [sleep](sleep/) | suspend execution for an interval | 72 | 14 |
| [sort](sort/) | sort, merge, or sequence check text files | 888 | 80 |
| [split](split/) | split a file into pieces | 224 | 35 |
| [strings](strings/) | find printable strings in files | 178 | 29 |
| [stty](stty/) | set the options for a terminal | 611 | 124 |
| [tabs](tabs/) | set terminal tabs | 912 | 47 |
| [tail](tail/) | copy the last part of a file | 359 | 28 |
| [tee](tee/) | duplicate standard input | 107 | 12 |
| [test](test/) | evaluate expression (with `[`) | 276 | 217 |
| [time](time/) | time a simple command | 194 | 27 |
| [timeout](timeout/) | execute a utility with a time limit | 464 | 70 |
| [touch](touch/) | change file access and modification times | 275 | 55 |
| [tput](tput/) | change terminal characteristics | 923 | 58 |
| [tr](tr/) | translate characters | 929 | 65 |
| [true](true/) | return true value | 13 | 2 |
| [tsort](tsort/) | topological sort | 263 | 20 |
| [tty](tty/) | return user's terminal name | 46 | 13 |
| [uname](uname/) | return system name | 65 | 16 |
| [unexpand](unexpand/) | convert spaces to tabs | 255 | 21 |
| [uniq](uniq/) | report or filter out repeated lines in a file | 164 | 19 |
| [unlink](unlink/) | call the unlink() function | 48 | 9 |
| [uudecode](uudecode/) | decode a binary file | 291 | 31 |
| [uuencode](uuencode/) | encode a binary file | 138 | 31 |
| [vi](vi/) | ex and vi, text editors (one program; vi and view are links) | 12173 | 485 |
| [wc](wc/) | word, line, and byte or character count | 178 | 18 |
| [xargs](xargs/) | construct argument lists and invoke utility | 630 | 70 |
| [yacc](yacc/) | yet another compiler compiler (with liby.a) | 3693 | 20 |

Lines: C, yacc and lex sources, without generated files and tests (96,983
in all).  Tests: what `make test` counts, 5650 in all; a count is of cases
or of case files, as each suite reports them.  Each README.md describes
what is implemented, the choices made where the standard leaves room, and
how it was tested, including the differential testing against other
implementations.

## Requirements

* A C11 compiler as `cc` (or `make CC=...`) and a POSIX system.  The
  collection builds with any POSIX make, including its own:
  `cd make && cc -O2 -o make *.c`, then `make/make` at the top.  With
  the compiler POSIX names, which takes no `-std=` or warning options:
  `make CC=c17 STDFLAGS=`.
* The tests: a POSIX shell, the POSIX utilities, a C compiler (for the
  small helpers in `testlib/`) and python3; the screen tests of vi and
  more also need the Python module pyte (they are skipped without it).

The tests use only what POSIX specifies: shell scripts are POSIX sh (no
bash), and the utilities they call are used with POSIX options only (no
`seq`, `mktemp`, `stat`, `readlink -f`, `sed -i` or `head -c`, for
example).  Where POSIX has no way to do something, `testlib/` has a small
C program for it: `fstat.c` prints a file's status (the conversions of
GNU `stat -c`, from lstat()), `ltouch.c` sets the times of a symbolic
link itself.  `testlib/ptyrun.py` runs a command with some of its
descriptors on a pseudo-terminal, typing `$PTYRUN_INPUT` on it when that
is set (for tty, nohup, stty, ps, and the questions of compress and
patch),
`testlib/ipc.c` makes the XSI IPC objects of ipcs and ipcrm,
`testlib/mkterminfo.py` writes the terminfo entries of tput and tabs, and
`testlib/testlib.py` holds what the Python runners share.

POSIX names no UTF-8 locale.  The tests that need one take
`$UTF8_LOCALE`, else C.UTF-8, else any UTF-8 locale `locale -a` lists;
with none, they are skipped (and not counted), and the rest run in the C
locale.  Files like `/dev/full` are used only where they exist.
* `make difftest`: the reference implementations named in each README.md
  (GNU awk, bc, date, dd, diff, ed, find, id, m4, make, od, patch, pr and
  sort; glibc's getconf and iconv; gzip; Christos Zoulas's file; ncurses
  tput and tabs; procps ps; psmisc fuser; flex; bison and byacc; dash,
  bash, yash, mksh and busybox (also its cal); nvi and vim; GNU tar,
  bsdtar, cpio; CSSC).

## Conventions

Every utility directory has the same parts:

| file | contents |
|------|----------|
| the sources | `name.c` for a single-file utility; several modules and a header otherwise |
| `Makefile` | targets `all` (the default), `test`, `difftest`, `install`, `clean` |
| `README.md` | the sections Build and test, What is implemented, Choices where the standard leaves room, Testing, Files |
| `test_name.sh` | the tests: `make test`, or `sh test_name.sh` from anywhere |
| `tests/` | for the larger utilities: the test cases, expected results and test programs |

Makefiles are POSIX makefiles (`.POSIX:`) of one pattern: `CC`, `CFLAGS`
(default `-O2`), `LDFLAGS`, `PREFIX`, `BINDIR` (and `DESTDIR`) can be set
on the command line; `STDFLAGS` holds what the sources need (`-std=c11`
and the warnings).  Each source defines its own feature-test macros
(`_POSIX_C_SOURCE`, `_XOPEN_SOURCE`) before any include, or includes the
project header that does; nothing depends on the makefile for them.

Options are read with getopt() as POSIX specifies it: scanning stops at
the first operand, so `cat f -u` reads a file named `-u` (with glibc this
holds because the sources define `_POSIX_C_SOURCE` and not `_GNU_SOURCE`;
every test script of a utility that uses getopt() checks it).  env, find,
xargs and sort read their options by hand: the first three because the
utility's or expression's arguments follow, sort because it may take
options after operands (it is excepted from Guideline 9, as are lex,
yacc and make).  m4 does too, as its options may also come after
operands, and pr, for +page and the options with optional arguments (-e,
-i, -n, -s); dd has operands only.  nice, time and timeout use getopt(),
which stops at the utility operand: what follows it is the utility's.

at and crontab come with the daemons that run their jobs, atd and crond
(POSIX specifies none), installed into `$(PREFIX)/sbin`.  For users other
than root, at, crontab and newgrp have to be installed set-user-ID root
(`make install` does not do that; see their README.md).

Each source file starts with a comment naming the file and what it holds
(`cut.c - cut: cut out selected fields of each line of a file`), followed
by the choices it makes where the standard leaves room.

Every test script ends with one line, `name: N passed, M failed`, and exits
0 only if nothing failed; `run_tests.sh` adds them up.  The suites of the
larger utilities each end with `N passed, M failed` too.  Frozen expected
results are recorded with the suite's `--freeze` (or `--update`) option.

Generated parsers and scanners (`*.tab.c`, `*.tab.h` and the lex output
of awk, bc and sh) are included, made with this collection's own yacc and
lex, so building needs only a C compiler.  They are newer than their
grammars; to regenerate them, delete them and run make (`YACC=`, `LEX=`).

Sanitizer builds go through `CC`:

    ASAN_OPTIONS=detect_leaks=0 \
    CC="cc -fsanitize=address,undefined -g" sh run_tests.sh

## Status

Done: the 113 utilities of the 100 directories above (ex with vi, [ with
test, batch with at, uncompress and zcat with compress, the ten of SCCS
in sccs), and in the shell all the special built-ins (break, colon,
continue, dot, eval, exec, exit, export, readonly, return, set, shift,
times, trap, unset) and the intrinsic utilities (alias, bg, cd, command,
fc, fg, getopts, hash, jobs, kill, read, type, ulimit, umask, unalias,
wait).

Still to do, 25 utilities:

* files: df, du
* users: lp, man, mesg, talk, who, write
* internationalization: gencat, gettext (ngettext), locale, localedef,
  msgfmt, xgettext
* development: ar, asa, c17, cflow, ctags, cxref, nm, strip
* UUCP: uucp, uustat, uux

[REVIEW.md](REVIEW.md) records the review of the smaller utilities
against the standard (October 2026) and what it changed.
