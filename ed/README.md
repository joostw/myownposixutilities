# ed — edit text (POSIX.1-2024)

A from-scratch implementation of the ed line editor as specified in XCU
ed (POSIX.1-2024), in C with no dependencies beyond libc (regcomp for
the basic regular expressions).

## Build and test

    make           # builds ./ed
    make test      # the tests (sh test_ed.sh): regression and terminal tests
    make difftest  # compares with GNU ed (needs /usr/bin/ed)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

Options -p string and -s, and the file operand (also "!command").

Commands: a c d e E f g G h H i j k l m n p P q Q r s t u v V w = !, and
the null command.  The l, n and p suffixes work, in any combination.

Addresses:
  * . $ n 'x /RE/ ?RE?, + and - with or without numbers, and offsets
    separated by blanks ("1 2 3" is line 6).
  * , and ; with omitted addresses on either side, as in the table of
    the standard (",," is $,$; "7;5;9" is 5,9).
  * More addresses than a command takes: they are evaluated, then
    discarded.
  * Searches wrap around the buffer; the closing delimiter can be
    omitted at the end of a line.

Regular expressions are BREs; the null RE is the previous one.  The
delimiter can be any character other than backslash, space and
newline.  An escaped delimiter is that literal character, and the
delimiter is ordinary inside bracket expressions ("g%[%]%p",
"s-[0-9]--g").

Substitution:
  * & and \1..\9, % alone for the previous replacement, and escaped
    delimiters.
  * Lines are split with an escaped newline, continued on the next
    input line.
  * Flags: count, g, l, n and p.  Without the closing delimiter, the
    result is printed.
  * Empty matches are handled as sed does: `s/x*/-/g` on "abc" gives
    "-a-b-c-".

Global commands:
  * g and v take a command list continued with backslash-newline; a,
    i and c with their text are allowed in the list, and the final
    "." may be omitted.  An empty list means p.  Lines changed by the
    list are unmarked.
  * G and V are interactive: an empty line does nothing and & repeats
    the last command.
  * A global command is undone as a single change.

Undo: u reverses the most recent change, including another u.  The
current line goes back to its value before that command.  After a
global command that changed nothing, there is nothing to undo.

Files and shell:
  * e, E, r and w take "!command".  w with !command does not change
    the "modified" state.  W appends, and wq writes and quits
    (extensions).
  * In !command, % is replaced by the remembered pathname (`\%` is a
    literal %) and a leading ! by the previous command.  The command
    is echoed when changed, and "!" is written afterwards unless -s
    is given.

The "modified" state is tri-state: unchanged, changed, and
changed-and-warned.  The first q or e on a changed buffer writes "?"
and warns; the second takes effect.  End of input acts like q.

l writes `\\` \a \b \f \r \t \v and `\$` as escapes, other non-printable
bytes as \ooo, and printable characters of the locale (multibyte
included) as they are.  It folds at 72 columns with `"\"` and ends
each line with $.

Signals: SIGINT stops the current command (printing, a global command,
a substitution or text input), writes "?" and returns to command mode.
SIGHUP writes a changed, non-empty buffer to ed.hup in the working
directory, or else $HOME/ed.hup, and exits.  SIGQUIT is ignored.

Errors write "?", followed by the message when H is on; h explains the
last one.  When input is a terminal, input typed ahead is discarded
after an error.  The exit status is 1 if any error occurred.

Limits: none besides memory, for lines and files alike.  Lines may
contain NUL bytes.

## Choices where the standard leaves room or GNU ed differs

* 0i inserts at the start of the buffer.  m and t without a destination
  use the current line, as historical eds do.  ^ is the same as -.
  The operand - is the historical form of -s.
* A nonexistent file for the operand or e gives a warning on standard
  error and an empty buffer that keeps the name.
* As in GNU ed:
  - the current line set by ; stays set even if the command then fails;
  - an explicit address out of range is an error even if it is
    discarded, but the implied 1 of "," is not checked (so ,a works
    on an empty buffer);
  - g on an empty buffer is an invalid address.
* Differences from GNU ed (tests/cases.py lists them):
  - digits are allowed as delimiters (GNU ed rejects them);
  - `s/x*/-/g` works (GNU ed: "infinite substitution loop");
  - \3 without a third subexpression is an error, as in sed (GNU ed
    makes it a literal 3);
  - u after a failed command undoes the most recent change (GNU ed
    forgets the undo);
  - after u, the buffer counts as modified, as the standard says (GNU
    ed restores the earlier state of the flag);
  - an unset mark is always an error (GNU ed takes it as line 0 in an
    empty buffer);
  - s inside g prints its l, n or p flags only for lines where it
    substituted;
  - printable UTF-8 characters are shown as they are by l;
  - text being typed when an interrupt arrives is discarded (GNU ed
    keeps the lines already entered);
  - no "Newline appended" message;
  - no GNU extensions such as x, y and z.

## Testing

* tests/difftest.py: 86 cases.  78 match GNU ed 1.20 exactly in output,
  exit status and written files.  The other 8 are listed with the
  reason they differ.
* tests/randdiff.py: random files and scripts compared with GNU ed.
  The generator avoids only the known differences above.  The last
  4,000 random scripts showed differences only from GNU ed's handling
  of marks in an empty buffer.
* tests/pty_tests.py: 13 tests on a pseudo-terminal: interrupting
  printing, input, g and G, interrupts at the prompt, SIGHUP and
  ed.hup, SIGQUIT, P, G with & and empty lines, discarding typed-ahead
  input, and !.
* Both suites pass under AddressSanitizer and UBSan.  7,000 mutated
  random scripts under ASan found nothing.
* A script of g, s, v and m over 200,000 lines takes 3 seconds (GNU ed:
  5 minutes), with identical results.

## Files

| file | contents |
|------|----------|
| `main.c` | addresses, commands, global commands, signals, main loop |
| `buf.c` | the buffer, marks, undo, reading and writing files |
| `re.c` | regular expressions, delimiters, replacement, substitution |
| `util.c` | errors, l/n/p output, input, shell commands |
| `tests/` | the test suites and their data (see Testing) |
| `test_ed.sh` | runs the tests: `make test`, or `sh test_ed.sh` |
| `Makefile` | build, test, difftest, install, clean |
