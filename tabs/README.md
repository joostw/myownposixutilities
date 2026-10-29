# tabs — set terminal tabs (POSIX.1-2024)

An implementation of the `tabs` utility of POSIX.1-2024 (XCU tabs), in C
with no dependencies beyond libc.

    tabs [-n|-a|-a2|-c|-c2|-c3|-f|-p|-s|-u] [-T type]
    tabs [-T type] n[[sep[+]n]...]

## Build and test

    make           # builds ./tabs
    make test      # the tests (sh test_tabs.sh)
    make difftest  # compares with ncurses tabs
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* clear_all_tabs, then set_tab at each column (reached with spaces after a
  carriage_return), from the terminal's compiled terminfo entry
  (`terminfo.c`, the same as tput's).
* The repetitive -0 to -9, the lists of -a to -u, and lists with blanks or
  commas and + increments.

## Choices where the standard leaves room

* List values are column numbers counted from 1 (as in historical tabs and
  ncurses); -n sets stops at columns 1, n+1, 2n+1, ...
* Stops up to the terminal's columns (80 without); a list stop past the last
  column is set there. The output is that of ncurses tabs, byte for byte.
* The last tab specification counts; a list may be several operands.
* The default terminal type is dumb. A terminal that cannot clear or set tab
  stops, an unknown type: status 1. A list that is not valid: status 2.

## Testing

`test_tabs.sh` runs the utility on prepared input and compares the output
and the exit status: terminfo entries compiled by `testlib/mkterminfo.py`
with visible strings: every option, lists, increments, clamping, TERM and
-T, terminals that cannot, invalid lists. The binary tested is `./tabs`, or
`$U` if set.

`tests/tabsdiff.sh` compares every option and some lists for every terminal
type of the system's terminfo directories with ncurses tabs, byte for byte.

## Files

| file | contents |
|------|----------|
| `tabs.c` | the utility |
| `terminfo.c` | reading compiled terminfo entries (the same file as in tput) |
| `terminfo.h` | its interface |
| `tests/tabsdiff.sh` | the comparison with ncurses tabs (`make difftest`) |
| `test_tabs.sh` | the tests: `make test`, or `sh test_tabs.sh` |
| `Makefile` | build, test, install, clean |
