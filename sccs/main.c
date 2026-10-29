/*
 * main.c - the SCCS utilities (POSIX.1-2024, XCU admin, delta, get, prs,
 * rmdel, sact, sccs, unget, val, what; XSI): one program that acts by
 * its name (sccs when it is none of the others)
 *
 * The SCCS file has the classic format (sfile.c), so the files can go
 * between these utilities, AT&T's, Schilling's and CSSC.
 *
 * Choices where the standard leaves room:
 *  - Dates are written with two-digit years (69 to 99 are 19yy) in the
 *    local time of TZ.  New SCCS files have mode 0444; changed ones keep
 *    theirs.  The g-file is 0444 with keywords replaced, else 0644 (less
 *    the mask); the l-file 0444, the p-file 0644.  New files get the flag
 *    "e 0" and the flags are written in the order of their letters, e
 *    last, as CSSC does.
 *  - A wrong checksum makes the file corrupted (an error) for all but
 *    admin -h (which reports it) and admin -z (which writes a new one).
 *  - admin: -m, -r and -y only with a new file, -r only with -i; -n alone
 *    makes delta 1.1 with no lines; users added go before those already
 *    there; -fl adds releases to the list and -dl takes them out (a: all).
 *  - A list of users that is not empty lets only those named (or in a
 *    group named) make deltas, also when it only names users with !.
 *  - get: a writable g-file in the way is an error.  No id keywords is a
 *    warning ("warning: s.f: No id keywords." on standard error), an error
 *    with the i flag, which with a value must also be in the text.  -t
 *    without -r is the newest delta of the highest release.  With -c the
 *    deltas after the cutoff are not applied, and the SID written is the
 *    one asked for.  A delta both included and excluded is included; when
 *    the delta got is excluded, the keywords are those of the nearest one
 *    before it that is applied.  The l-file has the deltas on the way to
 *    the one got and those included, excluded, ignored or cut off.
 *  - get -e: the floor and the ceiling apply to the release got, the
 *    locked releases to that of the new delta.  An edit of a SID being
 *    edited is refused unless the j flag is set, and the new SIDs in the
 *    p-file are taken: then a branch is made.
 *  - delta: without -y (and -m for the v flag) the comments (and MRs)
 *    are read from the standard input, after the prompt only on a
 *    terminal, once for all the files.  The v-flag program is run as
 *    "program module type MR...".  Null deltas (the n flag) have the
 *    comment AUTO NULL DELTA.  The differences are a shortest edit
 *    script, runs of changed lines moved down as GNU diff moves them, and
 *    new lines go just before the next line of the version edited, as
 *    CSSC places them.  -p writes them in the normal format of diff; the
 *    report has no file names.
 *  - prs: -d without -r, -e, -l or -c is for the newest delta; :PN: is the
 *    name as given (CSSC writes an absolute pathname); unset flags are
 *    "none" or "no" and an empty :Q:, :DI: is empty without lists, :B:
 *    and :S: are empty for a trunk delta; :GB: has its keywords replaced;
 *    :FL: has the descriptions of the historical prs.
 *  - rmdel: only a delta with no successor on any branch; it is kept as
 *    type R and what it did is taken out of the body.
 *  - sact: a file with no edits writes nothing (status 0).
 *  - what: strings found after one in the same line are part of it (the
 *    standard's ends only); status 1 when none is found.
 *  - sccs: an operand whose last component starts with s. is used as it
 *    is; no operands is the SCCS directory; the operands of what are not
 *    changed.  create runs admin for each file, renames it ,name, and
 *    gets them all with one get.  info writes "Nothing being edited"
 *    when nothing is; -b leaves out edits that make a branch delta (the
 *    standard's "SIDs with two or fewer components" reads as the opposite
 *    of what it means); -u user and -U.  clean removes the g-files that
 *    are not being edited.  diffs runs get -s -p -k into diff -, with a
 *    "------- name -------" line.  fix runs get -k, rmdel, then get -e -g
 *    of the delta before.  unedit runs unget and get ("name: removed").
 *    print is prs.  PROJECTDIR that is not a pathname names a user whose
 *    src (or else source) directory is used.  admin, rmdel, info, check,
 *    tell and clean always run as the real user.
 *  - Usage errors exit with status 2 and other errors with status 1, but
 *    for val (the bits of the standard) and what.
 */

#include "sccs.h"

#include <locale.h>
#include <string.h>

int main(int argc, char **argv)
{
    static const struct {
        const char *name;
        int (*fn)(int, char **);
    } tab[] = {
        { "admin", admin_main }, { "delta", delta_main }, { "get", get_main },
        { "prs", prs_main }, { "rmdel", rmdel_main }, { "sact", sact_main },
        { "unget", unget_main }, { "val", val_main }, { "what", what_main },
    };
    setlocale(LC_ALL, "");
    const char *name = base_name(argv[0]);
    for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (strcmp(name, tab[i].name) == 0) {
            progname = tab[i].name;
            return tab[i].fn(argc, argv);
        }
    progname = "sccs";
    return sccs_main(argc, argv);
}
