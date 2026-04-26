/*
 * posix_re — reusable multi-pattern matching for POSIX text utilities.
 *
 * A pattern set (pset_t) holds any number of patterns; a line matches if
 * ANY pattern in the set matches.  The three modes map directly to the three
 * grep personalities mandated by SUSv5 (IEEE Std 1003.1-2024):
 *
 *   PAT_BRE   — Basic Regular Expressions   (default grep)
 *   PAT_ERE   — Extended Regular Expressions (grep -E)
 *   PAT_FIXED — Fixed / literal strings      (grep -F)
 *
 * Patterns are compiled at add-time so that match-time is fast.
 * The module is locale-aware: regcomp/regexec honour LC_CTYPE / LC_COLLATE,
 * and the fixed-string case-insensitive path uses strncasecmp (XSI).
 *
 * Reuse notes: this module has no grep-specific knowledge.  Any utility that
 * needs "does this line match one of N patterns?" (sed, awk address ranges,
 * …) can use it directly.
 */

#ifndef POSIX_RE_H
#define POSIX_RE_H

#include <regex.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum { PAT_BRE, PAT_ERE, PAT_FIXED } pat_mode_t;

/* Opaque handle — allocate with pset_new(), release with pset_free(). */
typedef struct pset pset_t;

/*
 * pset_new — allocate an empty pattern set.
 *
 * mode  : PAT_BRE | PAT_ERE | PAT_FIXED
 * icase : if true, perform case-insensitive matching
 *
 * Returns NULL on ENOMEM (errno is set).
 */
pset_t *pset_new(pat_mode_t mode, bool icase);

/*
 * pset_add_str — add newline-separated patterns from a NUL-terminated string.
 *
 * Each segment between (or after) newlines is one pattern; an empty segment
 * is a null pattern that matches every line (per SUSv5: "a null BRE/ERE/
 * string shall match every line").
 *
 * src is used only in error messages (e.g. "-e argument", "pattern").
 *
 * Returns 0 on success, -1 on error (message written to stderr).
 */
int pset_add_str(pset_t *ps, const char *str, const char *src);

/*
 * pset_add_file — add patterns from a file, one per line.
 *
 * A trailing newline on the last line is consumed (patterns are
 * newline-terminated per SUSv5); a missing final newline is tolerated.
 *
 * Returns 0 on success, -1 on error (message written to stderr).
 */
int pset_add_file(pset_t *ps, const char *path);

/*
 * pset_match — test whether any pattern in the set matches the line.
 *
 * line : the text to match, NUL-terminated, without a trailing newline
 * len  : strlen(line) — passed so callers need not recompute it
 * whole: if true, the entire line must be consumed by the match
 *        (semantics of grep -x)
 *
 * Returns true on a match, false otherwise.
 */
bool pset_match(const pset_t *ps, const char *line, size_t len, bool whole);

/*
 * pset_free — release all resources owned by ps (including ps itself).
 * Safe to call with NULL.
 */
void pset_free(pset_t *ps);

#endif /* POSIX_RE_H */
