# pax — portable archive interchange (POSIX.1-2024)

A from-scratch implementation of the pax utility as specified in XCU
pax (POSIX.1-2024), in C with no dependencies beyond libc (iconv() for
the character sets of names; where iconv is a separate library, build
with make LDFLAGS=-liconv).

## Build and test

    make           # builds ./pax
    make test      # the tests (sh test_pax.sh): regression suite
    make difftest  # compares with GNU tar, bsdtar, GNU cpio, MirBSD pax (those installed)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## Modes and options

All four modes are implemented: list (no -r/-w), read (-r), write (-w)
and copy (-r -w).  All options in the synopsis are supported:

  -a -b -c -d -f -H -i -k -l -L -n -o -p -s -t -u -v -x -X

    -o   delete=, exthdr.name=, globexthdr.name=, invalid=, linkdata,
         listopt=, times, keyword=value (global) and keyword:=value
         (per file), with the precedence rules of "pax Extended Header
         Keyword Precedence" applied in read and list modes.
    -p   a e m o p, processed left to right.
    -s   ed-style /old/new/[gpsS], BREs, applied in order up to the first
         one that succeeds; a name that becomes empty is skipped.
    -i   prompts on /dev/tty: an empty reply skips the file, "." keeps
         the name, end of file exits.
    -v   list mode: ls -l style with "== target" for hard links and
         "-> target" for symbolic links; other modes: names on stderr.
         listopt= supports printf conversions with (keyword) and the
         extra conversions %T %M %D %F %L.

## Formats

    Written:  ustar (the default), pax, cpio (odc, magic 070707).
    Read:     ustar, pax (x and g headers), cpio odc, SVR4 newc/crc,
              pre-POSIX tar, and GNU tar's long name (L/K) headers.

The format is detected when reading; -x is only used when writing.

pax format writes an extended header only when one is needed: paths or
link names that do not fit or are not portable, sizes above 8 GiB,
IDs above 07777777, user/group names that are not alphanumeric,
sub-second modification times, and the -o keywords.

## Choices and differences from other implementations

* Patterns follow 2.14.3 exactly, so a leading period in a pathname
  component must be matched explicitly: `'*'` does not match "." or
  "./a".  MirBSD pax matches those.
* Trailing slashes on directory member names are removed when reading,
  so lists, patterns and -s see "dir", not "dir/" (as in MirBSD pax).
* Member names are used as they are.  Absolute names and ".." are not
  rewritten; use -s ',^/,,' to strip a leading slash.
* ustar: a pathname or link name that cannot be stored is reported and
  the file is skipped.  A user or group ID that does not fit is stored
  as 0 with a diagnostic.  cpio: values that overflow the odc fields
  are reported and the file is skipped.
* Copy mode does not copy the destination directory into itself; it is
  reported and its subtree is pruned.  The archive file itself is not
  archived in write mode.
* Without -p e or -p o, the S_ISUID and S_ISGID bits are never set.
  Directory modes and times are applied after their contents are
  extracted.
* -o linkdata writes hard links with their data, as the standard
  allows.  GNU tar does not accept such archives; bsdtar and this pax
  do.  When reading, a hard link whose target cannot be linked but
  whose data is present is extracted as a regular file.
* Character sets of names (pax format).  The path, linkpath, uname and
  gname records are UTF-8, translated from and to the codeset of the
  current locale with iconv():
  - Writing: names are translated to UTF-8.  A file with a name that
    cannot be translated gets hdrcharset=BINARY and its names as they
    are (whatever -o invalid= says; the standard defines that action for
    write mode only with binary).  -o hdrcharset=BINARY writes all
    names as they are.  ustar header fields always hold the bytes as
    they are.
  - Reading: names are translated from UTF-8, or from the character set
    a hdrcharset record names: BINARY means no translation, and the
    ISO 646 and ISO 8859 names of the standard's charset table are
    understood.  A path or link name that cannot be translated, holds a
    NUL, or is too long for the file system (NAME_MAX, PATH_MAX) is
    invalid, and -o invalid= decides, as the standard says: bypass (the
    default) skips the file, rename asks for a new name as -i does,
    binary uses the record's bytes, UTF-8 and write use the translation
    where it exists and the record's bytes where it does not.  A
    skipped file is reported on standard error and makes the exit
    status 1.  Names from ustar fields and cpio headers are used as they
    are.
  - Listing: every file is listed; a name that cannot be translated is
    written as the record holds it (the standard leaves this open).
    uname and gname are translated where possible and otherwise used as
    they are (implementation-defined in the standard).
  - Note that the default is bypass also in the C locale: with LC_ALL=C,
    files whose names hold characters outside ASCII in a pax archive
    written on a UTF-8 system are skipped.  This is the strict reading
    of the standard: the POSIX locale has 256 single-byte characters
    (XBD 6.2), but only the portable and control characters are
    defined, so a character such as U+00E9 has no counterpart there to
    be translated to.  Passing the UTF-8 bytes through would be an
    interpretation, which -o invalid=UTF-8 asks for explicitly.  Use a
    UTF-8 locale, or -o invalid=UTF-8 to extract them with their UTF-8
    names.  Archives from GNU tar with names that are not UTF-8 (it
    writes them without hdrcharset) need -o invalid=write in a UTF-8
    locale.
* Appending (-a) works for ustar, pax and odc archives, including ones
  written by other programs.  newc archives cannot be appended to.
* Truncated archives, bad checksums and unknown formats are diagnosed
  and make the exit status nonzero.
* Not supported: multi-volume archives, ACLs and extended attributes,
  sparse files.

## Testing

* tests/run.py: 19 regression scripts with frozen expected output,
  covering every mode and option (charset.sh: names in the C and a
  UTF-8 locale, hdrcharset, every -o invalid= action; skipped where
  there is no UTF-8 locale).  The suite also passes under
  AddressSanitizer and UBSan.
* tests/interop.sh (`make difftest`): 43 checks, a POSIX sh script
  that uses whichever of the other tools are installed.  It writes pax,
  ustar and cpio archives and extracts them with GNU tar, bsdtar, GNU
  cpio and MirBSD pax.  It reads the
  formats those tools write (gnu, oldgnu, posix, ustar, v7, newc, odc,
  crc, sv4cpio) and compares the extracted trees: contents, modes,
  times, hard links, symbolic links and device numbers.  It also
  appends to archives written by other tools and checks that -rw -pe
  copies the source tree exactly.
* 3200 mutated archives were read under ASan with no failures.
* Names in a Latin-1 and a UTF-8 locale were checked against bsdtar:
  archives written by either program in either locale extract to the
  same bytes with both.

## Files

| file | contents |
|------|----------|
| `main.c` | options and the four modes |
| `archive.c` | blocked archive input and output, format detection |
| `tar.c` | ustar and pax headers |
| `cpio.c` | cpio headers |
| `names.c` | patterns, -c/-n/-u selection, -s, -i |
| `charset.c` | character sets of names, -o invalid= |
| `extract.c` | creating files, -k/-u/-p |
| `list.c` | -v listings and listopt |
| `walk.c` | file hierarchy traversal, -H/-L/-X/-d |
| `util.c` | buffers, keyword lists, diagnostics |
| `tests/` | the test suites and their data (see Testing) |
| `test_pax.sh` | runs the tests: `make test`, or `sh test_pax.sh` |
| `Makefile` | build, test, difftest, install, clean |
