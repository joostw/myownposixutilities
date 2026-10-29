# tput — change terminal characteristics (POSIX.1-2024)

An implementation of the `tput` utility of POSIX.1-2024 (XCU tput), in C
with no dependencies beyond libc.

    tput [-T type] operand...

## Build and test

    make           # builds ./tput
    make test      # the tests (sh test_tput.sh)
    make difftest  # compares with ncurses tput
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* clear, init and reset from the terminal's compiled terminfo entry
  (`terminfo.c`: the legacy and the 32-bit formats, extended capabilities,
  the search path of TERMINFO, ~/.terminfo, TERMINFO_DIRS and the system
  directories).
* Any capability name, as in other implementations: booleans as the exit
  status, numbers written, strings written with the operands after them as
  parameters (the whole terminfo parameter language).

## Choices where the standard leaves room

* The default terminal type is dumb.
* init: init_1string, init_2string, the tab stops every 8 columns when the
  terminal's are not, init_file, init_3string; reset the same with the
  reset_ strings, each falling back to the init_ one. No programs are run
  and the modes are not changed.
* lines and cols: LINES and COLUMNS, else the window size, else terminfo.
* Statuses: 2 usage, 3 unknown terminal, 4 unknown capability or bad
  parameter, 5 write error; 1 for a false boolean or an absent string
  capability.

## Testing

`test_tput.sh` runs the utility on prepared input and compares the output
and the exit status: terminfo entries compiled by `testlib/mkterminfo.py`:
clear, init, reset, every operator of the parameter language, string
parameters, extended capabilities, 32-bit numbers, the search path, errors.
The binary tested is `./tput`, or `$U` if set.

`tests/tputdiff.py` compares every capability (strings with parameters too)
of every terminal type of the system's terminfo directories with ncurses
tput: about 22,000 cases.

## Files

| file | contents |
|------|----------|
| `tput.c` | the utility |
| `terminfo.c` | reading compiled terminfo entries, and the parameter language |
| `terminfo.h` | its interface |
| `tests/tputdiff.py` | the comparison with ncurses tput (`make difftest`) |
| `test_tput.sh` | the tests: `make test`, or `sh test_tput.sh` |
| `Makefile` | build, test, install, clean |
