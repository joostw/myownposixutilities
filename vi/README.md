# ex and vi — text editors (POSIX.1-2024)

An implementation of the `ex` and `vi` utilities of IEEE Std 1003.1-2024
(XCU, User Portability Utilities option): ex command mode and text input
mode, open mode and visual mode. One binary acts as `ex`, `vi` or `view`,
depending on the name it is invoked by. C11 and POSIX interfaces only, with no
curses; about 12,000 lines.

## Build and test

    make           # builds ./ex, with ./vi and ./view linked to it
    make test      # the tests (sh test_vi.sh): ex/vi regression and screen tests
    make difftest  # compares with nvi and vim
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

The screen tests and the vi differential tests drive the editor on a
pseudo-terminal and need python3 with the pyte module; without it they are
skipped.

## What is implemented

* **Invocation**: `-c` (repeatable, and `+command`), `-r` (with and without
  files), `-R`, `-s`, `-t`, `-v`, `-w`; standard input that is not a terminal
  implies `-s`; `EXINIT`, `$HOME/.exrc` and `./.exrc` (with the `exrc` option,
  ownership and permission checks, never twice); the `-c` commands run the
  first time an existing file is loaded, before a `-t` tag movement.
* **Addressing**: `.` `$` numbers, marks, `/re/` `?re?` with `wrapscan`,
  `+`/`-`/number offsets with intermediate out-of-range values, `,` `;` `%`,
  extra addresses discarded, the context mark rules.
* **Command parsing** exactly as the twelve steps describe: `|` and newline
  separation and the `||` rule, implied print with remembered `#`/`l` flags,
  `"` comments, the `d`+flags, `k`+mark and `s`+options name forms, the
  abbreviation table (`a`=append, `ma`=mark, `su`=suspend, ...), `!` argument
  handling for `r`/`w`/`!`, `+command` for `e`/`ex`/`n`, delimiters for
  `g`/`v`/`s`/`o`, `^V` escapes for `map`/`ab`, `a|text`.
* **All ex commands** of the standard, including `z` with every type, `&`
  and `~`, `s` with `g`/`c` (confirmation shows the line with carets),
  counts and flags, line splitting, `\u \l \U \L \e \E`, `%` and `~` in
  replacements, `edcompatible`; `g`/`v` as two-pass commands with multi-line
  command lists and text for `a`/`i`/`c`; `@`/`*` per addressed line; the
  `write` rules (read-only, existing files, pathname changed by `file`/`read`,
  partial buffers, `>>`, `!` and `writeany`); `%`, `#` and `!` expansion;
  shell expansion of file names through `$SHELL -c "echo ..."`; one-level
  undo that undoes itself, with marks restored on undone deletions.
* **Text input mode in ex**: autoindent, `^D`, `0^D`, `^^D`, the `number`
  prompt, interrupt keeping completed lines.
* **Errors**: in batch use (standard input not a terminal) the first error
  ends the session with exit status 1 and without writing; end-of-file on
  standard input, `SIGHUP` and `SIGTERM` preserve a modified buffer for
  `-r`; `SIGINT` aborts commands (and exits in batch use).
* **Recovery**: `preserve`, `recover`, `-r`; recovery files live in the
  `directory` option (default `/var/tmp`), the most recent one is used, older
  versions are reported, the user is mailed through `mailx`, and the files
  are removed after a complete write.
* **All edit options** of the standard, with their defaults (for example
  `paragraphs=IPLPPPQPP LIpplpipbp`, `report=5`, `scroll` and `window` from the
  screen size, `readonly` from `-R` and `access()`, reinitialized when the
  buffer is replaced unless set by the user).
* **Visual and open mode**: every command in "Command Descriptions in vi"
  with counts, buffers (`"a`..`"z`, appending with capitals, `"1`..`"9`
  shifted on multi-line and line-mode deletes and changes, `.` stepping
  through numeric buffers), and the per-motion rules for text regions and
  line/character mode (`w` vs `cw`, `dw` at the end of a line, `$` with a
  count, `%`, `` ` `` and `/` turning into line mode at line boundaries,
  `/re/+n`, `/re/z.`, `/re1/;/re2/`); words, bigwords, sentences, paragraphs
  and sections as defined (blank-line groups count as one entity); the
  remembered column; `^B ^D ^E ^F ^U ^Y` with the 2024 formulas; `H L M z`;
  `.` repeating with new counts; `u`, `U`; `@` with counts; `Q`;
  `^]` tags; `^^`; `:` with counts and a "Press any key" pause (`:` continues
  in ex) whenever ex output overwrites the screen.
* **Text input**: erase-columns as specified (the `c` region stays visible,
  ending in `$`, and erased characters stay until overwritten), `R`
  overwriting, counts repeating the whole input, autoindent with `^D`,
  `0^D`, `^^D`, `^T`, `^V`/`^Q`, `NUL`, the erase/kill/word-erase characters
  of the terminal, abbreviations with the three lhs rules, `map!`,
  `showmatch`, `wrapmargin`, `showmode`.
* **Maps**: multi-character left-hand sides with a timeout, `#1`..`#12`
  function keys, `remap`, the rule that a lhs at the start of its rhs is not
  remapped, map loops detected, errors discarding the rest of a map or
  buffer.
* **Display**: long lines folded, tabs truncated at the margin, `@` for lines
  that do not fit at the bottom, `~` after the end, `list` and `number`
  formats, multibyte characters (UTF-8) with `wcwidth`, `window` limiting
  the displayed lines, `SIGWINCH`, `SIGCONT` and `^Z` (terminal modes
  restored).

## Choices where the standard leaves room

* The terminal is driven with ANSI/VT100 sequences; terminal type `dumb`
  (or `-s`) means no open or visual mode.
* Error messages go to standard output on a terminal (in standout mode) and
  to standard error otherwise; informational messages are suppressed by
  `-s`, and so is `autoprint` (as in nvi and Vim).
* Regular expressions are matched leftmost-longest by an internal matcher;
  `\n` in a pattern matches a newline character, `\0` in a replacement means
  the whole match.
* `l` folds long lines with a trailing backslash; `p` writes tabs as tabs and
  other non-printable characters as `^X` or `\ooo`.
* Undo is the single-level undo of the standard (a second `u` redoes).
* The empty buffer at startup has no current pathname.
* The `s` command accepts the `r` option (use the last RE), `y` is accepted
  for `ya`, and `^T`/`^D` in repeated input behave as when typed.
* In text input mode `^H`, `^U` and `^W` may erase autoindent characters but
  not past the start of the current line.
* Cursor keys of an xterm-like terminal work as `h j k l` in command mode.
* Open mode shows the current line on the line above the status line of an
  addressable terminal; there is no support for hardcopy or glass terminals.
* After a yank with a backward line motion (`yk`) the column is kept, as in
  historical vi, nvi and Vim.
* A complete write is any write of the whole buffer to a non-terminal file,
  as the standard defines it, so writing the buffer to another file also
  clears the modified state.

## Testing

* `tests/exdiff.py` runs the 215 ex scripts in `tests/excases.txt` under this
  ex, nvi's ex and Vim's ex mode and prints the cases where this one agrees
  with neither. Each remaining difference was checked against the standard:
  they are output formats the standard fixes (`%6d  ` line numbers, `\t` and
  `\$` in `l` output, folding), behaviours where both other editors deviate
  (single-level undo, `s//` repeating the last substitution, `%` as the
  previous replacement, `j` and `@` with counts and ranges, errors ending a
  batch session), and Vim's silence in `-s` mode for implied prints, `=`
  and the like.
* `tests/vidiff.py` types the 246 keystroke sequences of `tests/vicases.txt`
  into this vi, nvi and Vim (in vi-compatible mode) through a pseudo-terminal
  and compares the resulting files. All agree with at least one of the two
  except three where the standard differs from both: `2r<CR>` makes two new
  lines, `3@q` behaves as if `3` was typed before the buffer contents, and
  `p` into an empty buffer leaves no empty line.
* `tests/rediff.py` compares 912 substitutions with sed(1) (leftmost-longest
  matching, global substitution, back-references): no differences.
* `tests/screentest.py` checks the display (folding, `@`/`~` lines, `number`,
  `list`, erase-columns, `showmode`, the wait prompt, resizing).
* `tests/regress.py` freezes the verified ex and vi results; `make test`
  runs it and the screen checks.
* A random keystroke fuzzer was run against the sanitizer build, and the
  suites pass under AddressSanitizer and UBSan.

## Files

| file | contents |
|------|----------|
| `ex.h`, `cmd.h`, `vi.h` | shared declarations |
| `util.c` | memory, strings, output, error unwinding, multibyte character helpers, `l`-format rendering |
| `buf.c` | the edit buffer (gap array of lines with stable identities), undo, marks, text buffers |
| `regex.c` | the ex regular expression matcher (BRE plus `\<`, `\>`, `~`, `magic`, `ignorecase`) and replacement strings |
| `options.c` | edit options and `set` |
| `excmd.c` | command-line parsing (steps 1–12 of "Command Line Parsing in ex"), addressing, the command table, input sources |
| `cmds.c` | the editing commands: `p l nu d y pu co t m j < > k u = z ^D a i c g v s & ~ @ *` |
| `exfile.c` | files and the argument list, `e n rew f r w wq x q`, shell escapes and filters, `preserve`/`recover`, tags, `cd so sh su` |
| `term.c` | terminal modes, keyboard input queue, `map`/`map!`/`abbreviate` |
| `screen.c` | the open/visual display: folding, `@` and `~` lines, the status line, ex output over the screen, the bottom-line editor |
| `vi.c` | visual and open mode commands, motions and operators |
| `vinput.c` | text input mode: autoindent, erase-columns, `^D ^T ^H ^U ^W ^V NUL`, abbreviations, `showmatch`, `wrapmargin` |
| `main.c` | options, initialization (`EXINIT`, `.exrc`), signals |
| `tests/` | the test suites and their data (see Testing) |
| `test_vi.sh` | runs the tests: `make test`, or `sh test_vi.sh` |
| `Makefile` | build, test, difftest, install, clean |
