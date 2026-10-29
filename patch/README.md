# patch — apply changes to files (POSIX.1-2024)

An implementation of the `patch` utility of POSIX.1-2024 (XCU patch), in C
with no dependencies beyond libc.

    patch [-blNR] [-c|-e|-n|-u] [-d dir] [-D define] [-i patchfile]
          [-o outfile] [-p num] [-r rejectfile] [file]

## Build and test

    make           # builds ./patch
    make test      # the tests (sh test_patch.sh)
    make difftest  # compares with GNU patch
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The four formats (copied context, unified context, normal and ed), found
  from each hunk or given by -c, -u, -n or -e; several patches in one file;
  indented patches; lines without a newline at the end.
* The names as the standard finds them (*** or --- then --- or +++, Index:,
  SCCS get -e, a question), with -p; new files from /dev/null.
* A hunk goes where its old lines are, searched outwards from its line
  number, then with fuzz 1 and 2 (the context ends left out); one that does
  not fit goes to the reject file. -R, the reversed-patch question, -N, -b,
  -o, -D, -l, -d and -r.

## Choices where the standard leaves room

* The informational messages go to standard error; standard output is only
  for the prompt for a file name.
* As in GNU patch, a hunk with less context at one end is taken to be at
  that end of the file, and the fuzz takes first from the end with more
  context.
* A new file is made when its old side is empty; with the new name /dev/null
  and an empty result the file is removed.
* Names end at a tab (or a blank); quoted names with C escapes are known.
* Without a name and without a terminal the patch is skipped (status 2); a
  hunk that fits only reversed is skipped with -N, and asked about on
  /dev/tty otherwise (no terminal: no).
* Rejected unified hunks are written as unified hunks, the others in copied
  context format, with the line numbers of the new file.
* -D marks changes "#ifndef define" (old), "#else" (new), "#endif";
  additions "#ifdef define"; deletions "#ifndef define".
* With -o the outfile is made anew and each version appended to it; files
  are written to a new file that is renamed over the old one, with its mode.
* The exit status is 0, 1 when a hunk was rejected, 2 on other errors and
  usage errors.

## Testing

`test_patch.sh` runs the utility on prepared input and compares the output
and the exit status: every format, found and forced, offsets, fuzz, -l,
rejects in both formats with -r, -R and reversed patches (-N, the question
on a terminal), the name rules with -p and -d, Index:, the prompt, new and
removed files, several patches, indented ones, -b, -o, -D, errors. The
binary tested is `./patch`, or `$U` if set.

`tests/patchdiff.py` makes about 250 random pairs of files and their
differences in eight diff formats: applied to the old file they must give
the new one (and -R the old one back), and applied to changed versions
(lines added, context changed) the result and the rejects must be those of
GNU patch --posix.

## Files

| file | contents |
|------|----------|
| `patch.c` | the utility |
| `tests/patchdiff.py` | the comparison with diff and GNU patch (`make difftest`) |
| `test_patch.sh` | the tests: `make test`, or `sh test_patch.sh` |
| `Makefile` | build, test, install, clean |
