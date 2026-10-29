# Review of the smaller utilities against POSIX.1-2024 (October 2026)

The 23 single-file utilities (basename, cat, cut, dirname, echo, false,
grep, head, ln, ls, mkdir, more, pwd, sed, sleep, tail, tee, touch, tr,
true, uniq, unlink, wc) were checked against their pages of the standard,
and each was given a test suite, `test_<utility>.sh`.  This records what
the review found and changed.  All 730 of their tests then passed, also
under AddressSanitizer and UBSan.  The larger utilities were already
reviewed and tested when they were written; their README.md files
describe how.

## Unchanged (verified, tests added)

true, false, echo, tee, unlink.

## Fixes

**basename, dirname**: a first `--` is discarded (XCU 1.4: utilities with
operands but no options); write errors give a non-zero exit status.

**sleep**: rewritten.  The operand must be digits only.  Large values
saturate instead of overflowing.  SIGALRM is ignored, and nanosleep resumes
after EINTR.

**pwd**: write errors checked; `-P` works for paths longer than the
initial buffer.

**cat**: rewritten with read/write, so `-u` (accepted) is unbuffered by
nature; messages name the file.

**head**: digit-only counts.  The header for a `-` operand names it.
Standard input can be named more than once.  Write errors are checked.

**tail**: rewritten.
- Counts: zero is valid, `+0` is not, and `-r` takes no sign.
- `-c` from the end keeps only `count` bytes in memory.
- `-n` from the end keeps only `count` lines in memory.
- `-f` is ignored when standard input is a pipe.
- `-r` combined with `-c` or `-f` is a usage error.

**wc**: characters counted with mbrtowc().  An invalid byte is not a
character but is part of a word.  A `-` operand is named in the output.

**uniq**: `-s` skips characters, not bytes.  An output operand of `-` is
standard output.  The `-d`/`-u` combination is fixed.  Write errors are
checked.

**ln**: the "same file" check compares directory entries (as the standard
says), not inodes, so hard links to the same file are no longer refused.

**mkdir**: `-m` with a symbolic mode follows chmod:
- with no *who*, bits in the umask are unaffected;
- `=` with no *who* clears all bits.

The new directory is never more permissive than the requested mode at any
moment.

**touch**
- `-r`, `-t` and `-d` are mutually exclusive.
- Impossible dates (`0231`) are rejected.
- Seconds `60` gives "one second after :59".
- A pre-Epoch time no longer looks like an error.
- Very long years no longer overflow.
- It no longer defines `_GNU_SOURCE`, which made glibc's getopt permute
  arguments: `touch file -c` treated `-c` as an option.

**cut**: rewritten.
- `-c` now counts characters; before, it counted bytes.
- `-b -n` adjusts each list element per line as specified.
- `-d` accepts a multibyte character.
- Lists are unlimited, and decreasing ranges are an error.
- Read and write errors are checked.

**tr**: rewritten for the characters of the locale.
- Multibyte characters work, and so do concatenated `\octal` escapes that
  encode one.
- `[x*n]`: `n` of zero means fill, a leading 0 means octal.  Before,
  strtol base 0 also accepted hex.
- `[:lower:]`/`[:upper:]` case conversion is recognised at the same
  position in both strings.
- `-c` orders the complement by value, `-C` by collation.
- The squeeze set is the last operand's array.
- Arrays are no longer capped at 256 entries.
- Decreasing ranges are an error.

**grep**
- `-q` beats `-l`, and `-l` beats `-c`, whatever their order (STDOUT section).
- A trailing newline in a pattern list makes an empty pattern, which
  matches every line.
- `-s` also silences read errors of unreadable files (directories).
- A selected last line without a newline is written with one.
- `-iF` falls back to bytes on invalid input.

**sed**: largely rewritten.
- `^` and `$` no longer match at embedded newlines.  The old code used
  REG_NEWLINE, so `N;s/^b/X/` wrongly matched.
- `s///g` no longer matches an empty string right after a match: `s/a*/x/g`
  on `baaac` gives `xbxcx`.  Matching uses REG_STARTEND, so `\<` and
  similar see their context.
- The delimiter does not end an RE inside a bracket expression.  An
  escaped delimiter is the literal character.
- The empty RE is the last RE *used* at run time.
- `y` and `l` work on multibyte characters.
- `$` is detected correctly across several files, including empty and
  missing ones.
- A missing input file is reported and skipped (exit status 2), instead
  of stopping sed.
- `D` restarts the cycle and flushes the append queue.
- `N` at the end quits without printing, as specified.
- The `t` flag is cleared whenever a line is read.
- Text for `a`, `i` and `c` removes backslashes.
- Errors are reported for:
  - extra characters after a command;
  - line address 0;
  - a back-reference beyond the RE's groups;
  - duplicate labels.
- Write errors are checked.
- Two choices that follow the standard where GNU sed differs:
  - `\n` in a replacement is `n`;
  - `l` writes a newline as `\012`.

**ls**
- The last option wins in each mutually-exclusive set: `-A|-a`, `-F|-p`,
  `-H|-L`, and the output format.
- `-1` no longer turns off long format.
- `-S`/`-t`/`-c`/`-u` decide the sort as the OPTIONS text describes.
- `-f` ignores `-r/-S/-t` wherever they appear.
- With no operand, ls behaves as if given `.`, so `ls -d` prints `.`.
- Bugs fixed:
  - the target of a dangling symlink was used uninitialised;
  - loop detection missed loops under `-RL`;
  - headers came out as `dir//sub`;
  - the column count was one too few;
  - multibyte names were aligned by bytes;
  - `-t` ignored nanoseconds;
  - `qsort` was called on an empty directory.

**more**: largely rewritten.
- Input is read as the display needs it, so `yes | more` works.
- Each screen shows `-n`−1 lines, with the prompt below them.
- Control characters are shown as `^X` and invalid bytes as `\ooo`,
  instead of reaching the terminal raw.
- Widths, tabs and wide characters are handled correctly.
- Overstrike bold and underline follow the standard's rules.
- Forward motion scrolls, and backward motion uses reverse index, when
  lines stay on the screen.
- Commands are read from standard error with read() (`/dev/tty` as the
  fallback).
- `-p` patterns come from the `-p` string.
- The prompt never wraps.
- Signals:
  - SIGWINCH and SIGCONT refresh the screen;
  - SIGINT, SIGTERM, SIGHUP and SIGQUIT restore the terminal, then take
    their default action;
  - SIGTSTP suspends cleanly.
- `:e` on an inaccessible file leaves the file and screen unchanged.
- `-t` with a missing line shows a message and runs no `-p` for that file.
- Filter mode reports missing files and write errors in its exit status,
  and no longer treats a 1-byte last line as blank.
- At the end of the *last* file, any command other than
  f/space/j/newline/d/s exits, as the EXTENDED DESCRIPTION says.  `less`
  in more mode behaves differently.

## Interpretations worth knowing

Where the standard leaves a choice, the header comment of each source file
says what was done.  For example: tr pads a short string2 with its last
character, and cut rejects decreasing ranges.
