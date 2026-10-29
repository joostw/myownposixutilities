# more — display files on a page-by-page basis (POSIX.1-2024)

An implementation of the `more` utility of POSIX.1-2024 (XCU more), in C
with no dependencies beyond libc.

    more [-ceisu] [-n number] [-p command] [-t tagstring] [file...]

## Build and test

    make           # builds ./more
    make test      # the tests (sh test_more.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* If standard output is not a terminal, the files are copied to it as they
  are (-s squeezes empty lines), and nothing else.
* On a terminal, all the commands of the standard: scrolling by lines, half
  and whole screens, g G s, marks, searches (`/`, `?`, `!`, n N), :e :n :p
  :t, v, = and the help screen.
* Input is read as far as the display needs it, so a pipe that never ends
  (`yes | more`) is paged as it arrives; backward motion works on pipes too.
* Lines are folded at the screen width; wide characters are not split;
  overstriking makes bold and underlined text; other control characters are
  written as `^X`, and bytes that are not characters as `\ooo`.
* Forward motion scrolls the terminal and backward motion uses reverse
  index, when the screen is the whole terminal; otherwise, and with -c, the
  screen is redrawn.
* -n, LINES, COLUMNS and MORE as specified; SIGWINCH and SIGCONT refresh the
  screen.

## Choices where the standard leaves room

* Commands are read from standard error, or from /dev/tty if standard error
  cannot be read.
* At the end of the last file, any command other than f, ^F, space, j,
  newline, d, ^D and s exits, as the EXTENDED DESCRIPTION says (unlike
  `less`).
* The current position (for m, ' and '') is the top line of the screen.
* SIGINT, SIGQUIT, SIGTERM, SIGHUP and SIGTSTP keep their default actions,
  with the terminal modes restored first.
* The terminal is driven with ANSI (ECMA-48) sequences; tags are read from
  the file `tags` (ctags format).

## Testing

`test_more.sh` runs the utility on prepared input and compares the output
and the exit status: the filter mode in the shell, and the terminal
behaviour on a pseudo-terminal through a screen emulator (python3 with pyte;
skipped without it): every command, screen sizes, scrolling, character
display, several files, -p, -t, the editor, signals. The binary tested is
`./more`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `more.c` | the utility |
| `test_more.sh` | the tests: `make test`, or `sh test_more.sh` |
| `Makefile` | build, test, install, clean |
