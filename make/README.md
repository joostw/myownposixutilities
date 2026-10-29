# make — maintain, update, and regenerate files (POSIX.1-2024)

A from-scratch implementation of the make utility as specified in XCU
make (POSIX.1-2024), in C with no dependencies beyond libc.

## Build and test

    make           # builds ./make
    make test      # the tests (sh test_make.sh): regression suite
    make difftest  # compares with GNU make (needs gmake)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

Without a make, it builds with `cc -O2 -o make *.c`; it builds itself and
every other utility of this collection. `python3 tests/difftest.py ./make
--ref=/usr/bin/bmake` compares with bmake instead of GNU make.

## What is implemented

Options: -e -f -i -j -k -n -p -q -r -S -s -t, which may follow
operands (the standard exempts make from Guideline 9), and MAKEFLAGS in
both the letters and the dashed forms, including macro definitions.

Macros:
  * Forms: =, ::=, :::=, !=, ?= and +=, the last appending expanded or
    unexpanded text depending on the flavour of the existing macro.
  * Expansion: nested names ($($(X))), $(X:old=new), and pattern
    substitution $(X:op%os=np%ns).
  * Internal macros: $@ $% $< $* $? $^ $+, and their D and F forms.
  * Precedence of sources: command line, then MAKEFLAGS, the
    environment, the makefile and the built-ins; -e lets the
    environment win over the makefile.
  * Special macros: MAKE, SHELL and CURDIR.
  * Self-referencing macros are diagnosed.

Rules:
  * Target rules with ";" commands; command prefixes - @ +; and
    backslash-newline handled as specified in commands and elsewhere.
  * Inference rules: single and double suffix, the empty rule ".x.y: ;",
    and .SUFFIXES ordering.
  * Library members lib(member.o), with the .s2.a rules, $% and touching
    members under -t.
  * All the built-in rules and macros of the standard, including the
    SCCS (~) rules, .SCCS_GET, PROJECTDIR, and s.makefile/SCCS/s.Makefile
    as default makefiles (they need get).

Special targets: .DEFAULT .IGNORE .NOTPARALLEL .PHONY .POSIX .PRECIOUS
.SCCS_GET .SILENT .SUFFIXES .WAIT.

Include lines: include and -include, with several files and patterns on
one line and nesting to depth 64.  Include files use the "immediate
remaking" method: a rule parsed before the include line is used to make
the file before it is read, and those commands run even under -n, -q
and -t.

Parallel builds:
  * -j N runs up to N jobs at once, using a pool of N-1 tokens kept in
    a pipe.
  * Sub-makes share the pool when they are invoked through $(MAKE) or a
    "+" line and get the same -j value.
  * The pool uses GNU make's --jobserver-auth=R,W protocol, so GNU make
    and this make share one pool as parent or child.
  * .WAIT and .NOTPARALLEL are honoured.

Signals: SIGHUP, SIGINT, SIGQUIT and SIGTERM remove the target being
made if it changed, unless it is phony, precious or a directory.  Then
make re-raises the signal.  Signals that were ignored when make started
stay ignored, and with -n, -p or -q make does not catch them.

## Extensions (allowed by the standard)

  * Pattern rules: %.o: %.c, tried before suffix rules.
  * Double-colon rules: t:: a.
  * ":=" as a synonym for "::=", and "sinclude".
  * If no .s2.a rule applies to lib(member.o), the member file itself is
    inserted with $(AR) $(ARFLAGS) $@ $%.
  * An inference rule may use a prerequisite that has a target rule even
    if the file does not exist yet.  Inference rules are not chained.

## Choices and differences from other implementations

* Timestamps are compared to the nanosecond.  A prerequisite with
  exactly the same time as its target makes the target out-of-date.  The
  standard encourages this; GNU make and bmake do the opposite.  The
  exception is two names that are the same file (a symbolic link to its
  prerequisite), which count as up-to-date.  Archive members only have
  whole seconds, so a comparison involving a member uses seconds and
  equal counts as up-to-date.
* With deterministic ar (the default on many systems), member times
  are 0, so members always look out-of-date.  Set ARFLAGS to include U
  (-rvU) to keep real times.
* The standard says CC=c17 and CFLAGS=-O 1.  When c17 is not on PATH,
  make uses CC=cc and CFLAGS=-O1 instead.
* -q runs "+" lines, as the standard requires (GNU make does not).
  -t also runs lines that use $(MAKE).
* When errors are ignored (-i, "-", .IGNORE), commands run without
  sh -e, as the standard requires.  GNU make keeps -e for .IGNORE.
* The built-in .c.a and .sh rules are exactly those of the standard
  (cp, not cat; the object file is removed after ar).
* Prerequisites accumulate in the order the rules appear.  GNU make
  moves those of the rule with commands to the front.
* Messages follow GNU make's wording ("make: *** [t] Error 1",
  "No rule to make target ...", "'t' is up to date.").
* Not implemented: GNU and BSD conditionals and functions, target-
  specific macros, and chains of inference rules.

## Testing

* tests/difftest.py: 58 cases run with this make and with GNU make 4.3,
  comparing output and exit status.  45 are identical; the other 13 are
  listed in tests/cases.py with the reason they differ, such as .WAIT
  and :::=, which GNU make 4.3 lacks, and the differences listed above.
* tests/run.py: 14 regression scripts with frozen output, for behaviour
  the reference makes cannot check: equal timestamps, -q/-t/-n details,
  MAKEFLAGS formats, SCCS with a stand-in get, -p, measured job
  concurrency including shared pools, signals, deep includes, archives,
  :: and pattern rules, and parsing.
* Both suites pass under AddressSanitizer and UBSan.  3,000 mutated
  makefiles (12,000 runs) under ASan found nothing.
* Real builds: suckless sbase (a large .POSIX makefile) and Lua, with
  -j 8 and incrementally, and the pax, sh, awk and vi projects.

## Files

| file | contents |
|------|----------|
| `main.c` | options, MAKEFLAGS, macro sources, built-in rules, -p |
| `parse.c` | makefile syntax, include lines, rules, special targets |
| `var.c` | macros and their expansion |
| `build.c` | rule selection, out-of-date checks, jobs, -j, signals |
| `node.c` | target and inference rule tables |
| `ar.c` | archive library members |
| `util.c` | strings, lists, diagnostics |
| `tests/` | the test suites and their data (see Testing) |
| `test_make.sh` | runs the tests: `make test`, or `sh test_make.sh` |
| `Makefile` | build, test, difftest, install, clean |
