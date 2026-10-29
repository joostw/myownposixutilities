# sed — stream editor (POSIX.1-2024)

An implementation of the `sed` utility of POSIX.1-2024 (XCU sed), in C with
no dependencies beyond libc.

    sed [-En] script [file...]
    sed [-En] -e script [-e script]... [-f script_file]... [file...]
    sed [-En] [-e script]... -f script_file [-f script_file]... [file...]

## Build and test

    make           # builds ./sed
    make test      # the tests (sh test_sed.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* All the commands, addresses and flags of the standard, BREs and (-E) EREs,
  scripts from -e, -f and the operand joined as specified, `#n`.
* `^` and `$` match only at the ends of the pattern space; `\n` matches an
  embedded newline.
* The delimiter does not end an RE inside a bracket expression; an escaped
  delimiter is the literal character.
* An empty RE is the last RE used at run time.
* s///g does not match an empty string right after a match, and sees the
  context of each match (`\<`).
* y and l work on the characters of the locale.
* `$` is the last line of all the input, across empty and unreadable files.

## Choices where the standard leaves room

* The t flag is cleared whenever a line is read (also by n and N).
* N with no next line quits without writing the pattern space; n and q write
  it.
* In a replacement, `\n` is an `n` (POSIX), not a newline as in GNU sed; l
  writes a newline as `\012`.
* l folds at 70 columns.
* An input file that cannot be read is reported and skipped (exit status 2);
  a write error gives status 4.
* Extensions: `a text`, `i text`, `c text` on one line; labels also end at
  `;`; the w file `/dev/stdout` is the standard output.

## Testing

`test_sed.sh` runs the utility on prepared input and compares the output and
the exit status: every command, addresses and ranges, the RE rules, s flags
and empty matches, hold space, D/N/P cycles, branches, text commands, w and
r files, l, y on UTF-8, script syntax errors, several files, write errors.
The binary tested is `./sed`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `sed.c` | the utility |
| `test_sed.sh` | the tests: `make test`, or `sh test_sed.sh` |
| `Makefile` | build, test, install, clean |
