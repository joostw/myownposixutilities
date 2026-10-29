/*
 * patch.c - patch: apply changes to files (POSIX.1-2024, XCU patch)
 *
 * Synopsis:
 *   patch [-blNR] [-c|-e|-n|-u] [-d dir] [-D define] [-i patchfile]
 *         [-o outfile] [-p num] [-r rejectfile] [file]
 *
 * The patch file is split into patches: header lines (Index:, *** and
 * --- or --- and +++, anything else is ignored), then hunks of one kind:
 * copied context (***************), unified context (@@), normal (1,2c3)
 * or ed (1,2c).  A hunk is applied where its old lines are found: at its
 * line number (plus the lines added by the hunks before it and the
 * offset at which the last one was found), else at the nearest place
 * after the changes of the hunk before it, else, for context hunks, with
 * the first and last line of context left out, then two of each (the
 * fuzz).  As in GNU patch, a hunk with less context at one end than at
 * the other is taken to be at that end of the file, and the fuzz takes
 * first from the end with more context.  A hunk that does not fit goes
 * to the reject file.  ed hunks are run as ed commands (a, i, c, d and
 * s, as diff -e writes them).
 *
 * Choices where the standard leaves room:
 *  - The informational messages ("patching file f", "Hunk #2 succeeded at
 *    40 (offset 3 lines)") go to standard error; standard output is only
 *    for the prompt for a file name.
 *  - A patch whose old side is empty everywhere (--- /dev/null, or 0,0
 *    ranges) makes its file when none of the names exist: the new name
 *    is used.  When the new name is /dev/null and the result is empty,
 *    the file is removed.
 *  - Names are taken up to a tab (or, without one, a blank); names in
 *    double quotes with C escapes, as GNU diff writes them, are known.
 *  - When no name is found, the prompt "File to patch: " is written, and
 *    the answer read from /dev/tty; without one the patch is skipped
 *    (status 2).
 *  - A hunk that fits only reversed, before any hunk has been applied:
 *    with -N the patch is skipped; otherwise the question "Reversed (or
 *    previously applied) patch detected!  Assume -R? [y]" is asked on
 *    /dev/tty, and without a terminal the answer is no (the hunk is then
 *    rejected).
 *  - Rejected unified hunks are written as unified hunks, the others in
 *    copied context format; the line numbers are those of the new file.
 *    With -r all the rejects go to that one file.
 *  - -D marks changes as "#ifndef define" (old) "#else" (new) "#endif",
 *    additions as "#ifdef define", deletions as "#ifndef define".
 *  - With -o the outfile is written anew (after being saved as
 *    outfile.orig with -b), and each patched version appended to it.
 *  - Files are written to a new file next to them which is then renamed,
 *    with the mode of the old one.  With -l, matched lines keep the text
 *    of the file.
 *  - The exit status is 0, 1 when a hunk was rejected, 2 on other
 *    errors (and usage errors).
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "patch: %s\n", strerror(ENOMEM));
        exit(2);
    }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) {
        fprintf(stderr, "patch: %s\n", strerror(ENOMEM));
        exit(2);
    }
    return p;
}

static char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static char *xstrdup(const char *s)
{
    char *p = xmalloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

/* ---- lines ---- */

struct line {
    char *s;                    /* without the newline */
    bool noeol;                 /* the last line, without a newline */
};

struct text {
    struct line *l;
    size_t n, cap;
};

static void add_line(struct text *t, const char *s, size_t n, bool noeol)
{
    if (t->n == t->cap) {
        t->cap = t->cap ? 2 * t->cap : 64;
        t->l = xrealloc(t->l, t->cap * sizeof *t->l);
    }
    char *c = xmalloc(n + 1);
    memcpy(c, s, n);
    c[n] = '\0';
    t->l[t->n].s = c;
    t->l[t->n].noeol = noeol;
    t->n++;
}

static void free_text(struct text *t)
{
    for (size_t i = 0; i < t->n; i++)
        free(t->l[i].s);
    free(t->l);
    t->l = NULL;
    t->n = t->cap = 0;
}

static bool read_text(FILE *f, struct text *t)
{
    char *s = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&s, &cap, f)) > 0) {
        bool nl = s[n - 1] == '\n';
        add_line(t, s, (size_t)(nl ? n - 1 : n), !nl);
    }
    free(s);
    return !ferror(f);
}

/* ---- options ---- */

static bool opt_b, opt_l, opt_N, opt_R;
static char fmt_opt;            /* c, e, n, u, or 0 */
static const char *opt_D, *opt_o, *opt_r, *operand;
static int opt_p = -1;
static int status;

static void set_status(int s)
{
    if (s > status)
        status = s;
}

/* ---- the patch file ---- */

static struct text pf;          /* all its lines */
static size_t pl;               /* the next line */
static char *indent;            /* of the current patch */
static size_t indent_len;

/* line i of the patch file without the indent, or NULL past the end or
   when the indent is missing */
static const char *P(size_t i)
{
    if (i >= pf.n)
        return NULL;
    const char *s = pf.l[i].s;
    if (indent_len && strncmp(s, indent, indent_len) != 0)
        return NULL;
    return s + indent_len;
}

static bool is_unified_header(const char *s, long *a, long *b, long *c, long *d)
{
    *b = *d = 1;
    if (strncmp(s, "@@ -", 4) != 0)
        return false;
    char *e;
    *a = strtol(s + 4, &e, 10);
    if (e == s + 4)
        return false;
    if (*e == ',')
        *b = strtol(e + 1, &e, 10);
    if (strncmp(e, " +", 2) != 0)
        return false;
    const char *f = e + 2;
    *c = strtol(f, &e, 10);
    if (e == f)
        return false;
    if (*e == ',')
        *d = strtol(e + 1, &e, 10);
    return strncmp(e, " @@", 3) == 0;
}

/* "a[,b]" + command letter + optional "c[,d]"; for normal, the second
   range is required, for ed it must not be there */
static bool is_command(const char *s, bool ed, long *a, long *b, char *cmd, long *c, long *d)
{
    char *e;
    if (!isdigit((unsigned char)*s))
        return false;
    *a = strtol(s, &e, 10);
    *b = *a;
    if (*e == ',') {
        if (!isdigit((unsigned char)e[1]))
            return false;
        *b = strtol(e + 1, &e, 10);
    }
    if (*e != 'a' && *e != 'c' && *e != 'd' && !(ed && (*e == 'i' || *e == 's')))
        return false;
    *cmd = *e++;
    if (ed)
        return *e == '\0' || *cmd == 's';
    if (!isdigit((unsigned char)*e))
        return false;
    *c = strtol(e, &e, 10);
    *d = *c;
    if (*e == ',') {
        if (!isdigit((unsigned char)e[1]))
            return false;
        *d = strtol(e + 1, &e, 10);
    }
    return *e == '\0';
}

static bool is_context_header(size_t i)
{
    const char *s = P(i), *t = P(i + 1);
    return s && t && strncmp(s, "***************", 15) == 0 && strncmp(t, "*** ", 4) == 0;
}

/* the kind of hunk that starts at line i, or 0 */
static char hunk_kind(size_t i)
{
    const char *s = P(i);
    long a, b, c, d;
    char cmd;
    if (!s)
        return 0;
    if (is_context_header(i))
        return 'c';
    if (is_unified_header(s, &a, &b, &c, &d))
        return 'u';
    if (is_command(s, false, &a, &b, &cmd, &c, &d))
        return 'n';
    if (is_command(s, true, &a, &b, &cmd, &c, &d))
        return 'e';
    return 0;
}

/* ---- hunks ---- */

struct hline {
    char type;                  /* ' ', '-' or '+' */
    char *s;
    bool noeol;
};

struct hunk {
    char kind;                  /* c, u, n */
    long old_start, old_n, new_start, new_n;
    struct hline *l;
    size_t n, cap;
};

static void hadd(struct hunk *h, char type, const char *s, bool noeol)
{
    if (h->n == h->cap) {
        h->cap = h->cap ? 2 * h->cap : 32;
        h->l = xrealloc(h->l, h->cap * sizeof *h->l);
    }
    h->l[h->n].type = type;
    h->l[h->n].s = xstrdup(s);
    h->l[h->n].noeol = noeol;
    h->n++;
}

static void free_hunk(struct hunk *h)
{
    for (size_t i = 0; i < h->n; i++)
        free(h->l[i].s);
    free(h->l);
    memset(h, 0, sizeof *h);
}

static bool no_newline_mark(size_t i)
{
    const char *s = P(i);
    return s && s[0] == '\\' && s[1] == ' ';
}

static void bad_patch(const char *what)
{
    fprintf(stderr, "patch: line %zu: %s\n", pl + 1, what);
    exit(2);
}

static void parse_unified(struct hunk *h)
{
    long a, b, c, d;
    is_unified_header(P(pl), &a, &b, &c, &d);
    pl++;
    h->kind = 'u';
    h->old_start = a, h->old_n = b, h->new_start = c, h->new_n = d;
    long olds = 0, news = 0;
    while (olds < b || news < d) {
        const char *s = P(pl);
        if (!s)
            bad_patch("the unified hunk ends early");
        char t = s[0];
        if (t == '\0')          /* an empty context line, its blank lost */
            t = ' ', s = " ";
        if (t != ' ' && t != '-' && t != '+')
            bad_patch("not a line of a unified hunk");
        pl++;
        bool ne = no_newline_mark(pl);
        if (ne)
            pl++;
        hadd(h, t, s + 1, ne);
        if (t != '+')
            olds++;
        if (t != '-')
            news++;
    }
    if (olds != b || news != d)
        bad_patch("the line counts of the unified hunk are wrong");
}

/* one part of a context hunk: the lines with their two-character marks */
static void context_part(long n, struct hunk *part, const char *stop)
{
    for (long k = 0; k < n; k++) {
        const char *s = P(pl);
        if (!s || (stop && strncmp(s, stop, strlen(stop)) == 0 && (s[3] == ' ' || isdigit((unsigned char)s[4]))))
            break;
        char t = s[0];
        if (!strchr(" -+!", t) || (s[1] != ' ' && s[1] != '\0'))
            break;
        pl++;
        bool ne = no_newline_mark(pl);
        if (ne)
            pl++;
        hadd(part, t, s[1] ? s + 2 : "", ne);
    }
}

static bool range(const char *s, const char *mark, long *a, long *n)
{
    char *e;
    if (strncmp(s, mark, strlen(mark)) != 0)
        return false;
    s += strlen(mark);
    *a = strtol(s, &e, 10);
    if (e == s)
        return false;
    long b = *a;
    if (*e == ',')
        b = strtol(e + 1, &e, 10);
    *n = b == 0 && *a == 0 ? 0 : b - *a + 1;
    if (*a == 0 && b == 0)
        *n = 0;
    return true;
}

static void parse_context(struct hunk *h)
{
    pl++;                       /* *************** */
    long a = 0, an = 0, c = 0, cn = 0;
    if (!range(P(pl), "*** ", &a, &an))
        bad_patch("not a context hunk");
    pl++;
    struct hunk o = { 0 }, nw = { 0 };
    context_part(an, &o, "--- ");
    if (!P(pl) || !range(P(pl), "--- ", &c, &cn))
        bad_patch("the context hunk has no new part");
    pl++;
    context_part(cn, &nw, "***************");
    h->kind = 'c';
    /* "a" with no lines: an insertion after line a */
    h->old_start = a, h->old_n = an, h->new_start = c, h->new_n = cn;
    if (o.n == 0)       /* the old part left out: the new part's context */
        for (size_t i = 0; i < nw.n; i++)
            if (nw.l[i].type != '+')
                hadd(&o, nw.l[i].type == '!' ? '!' : ' ', nw.l[i].s, nw.l[i].noeol);
    if (nw.n == 0)
        for (size_t i = 0; i < o.n; i++)
            if (o.l[i].type != '-')
                hadd(&nw, o.l[i].type == '!' ? '!' : ' ', o.l[i].s, o.l[i].noeol);
    /* merge the two parts */
    size_t i = 0, j = 0;
    while (i < o.n || j < nw.n) {
        if (i < o.n && o.l[i].type == '-') {
            hadd(h, '-', o.l[i].s, o.l[i].noeol), i++;
        } else if (j < nw.n && nw.l[j].type == '+') {
            hadd(h, '+', nw.l[j].s, nw.l[j].noeol), j++;
        } else if (i < o.n && o.l[i].type == '!') {
            while (i < o.n && o.l[i].type == '!')
                hadd(h, '-', o.l[i].s, o.l[i].noeol), i++;
            while (j < nw.n && nw.l[j].type == '!')
                hadd(h, '+', nw.l[j].s, nw.l[j].noeol), j++;
        } else if (j < nw.n && nw.l[j].type == '!') {
            while (j < nw.n && nw.l[j].type == '!')
                hadd(h, '+', nw.l[j].s, nw.l[j].noeol), j++;
        } else if (i < o.n && j < nw.n) {
            /* context in both */
            hadd(h, ' ', o.l[i].s, nw.l[j].noeol);
            i++, j++;
        } else if (i < o.n) {
            hadd(h, ' ', o.l[i].s, o.l[i].noeol), i++;
        } else {
            hadd(h, ' ', nw.l[j].s, nw.l[j].noeol), j++;
        }
    }
    free_hunk(&o);
    free_hunk(&nw);
    long olds = 0, news = 0;
    for (size_t k = 0; k < h->n; k++) {
        if (h->l[k].type != '+')
            olds++;
        if (h->l[k].type != '-')
            news++;
    }
    h->old_n = olds, h->new_n = news;
    if (olds == 0)
        h->old_start = a;       /* after line a */
}

static void parse_normal(struct hunk *h)
{
    long a, b, c, d;
    char cmd;
    is_command(P(pl), false, &a, &b, &cmd, &c, &d);
    pl++;
    h->kind = 'n';
    if (cmd != 'a') {
        for (long k = a; k <= b; k++) {
            const char *s = P(pl);
            if (!s || strncmp(s, "< ", 2) != 0)
                bad_patch("a normal hunk: a < line is missing");
            pl++;
            bool ne = no_newline_mark(pl);
            if (ne)
                pl++;
            hadd(h, '-', s + 2, ne);
        }
    }
    if (cmd == 'c') {
        if (!P(pl) || strcmp(P(pl), "---") != 0)
            bad_patch("a normal hunk: --- is missing");
        pl++;
    }
    if (cmd != 'd') {
        for (long k = c; k <= d; k++) {
            const char *s = P(pl);
            if (!s || strncmp(s, "> ", 2) != 0)
                bad_patch("a normal hunk: a > line is missing");
            pl++;
            bool ne = no_newline_mark(pl);
            if (ne)
                pl++;
            hadd(h, '+', s + 2, ne);
        }
    }
    h->old_start = a;
    h->old_n = cmd == 'a' ? 0 : b - a + 1;
    h->new_start = c;
    h->new_n = cmd == 'd' ? 0 : d - c + 1;
}

static void reverse(struct hunk *h)
{
    for (size_t i = 0; i < h->n; i++)
        if (h->l[i].type != ' ')
            h->l[i].type = h->l[i].type == '-' ? '+' : '-';
    long a = h->old_start, b = h->old_n;
    h->old_start = h->new_start, h->old_n = h->new_n;
    h->new_start = a, h->new_n = b;
}

/* ---- names ---- */

/* the name in a ***, --- or +++ line (after the mark), malloc'd */
static char *header_name(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    char *r = xmalloc(strlen(s) + 1);
    size_t n = 0;
    if (*s == '"') {
        for (s++; *s && *s != '"'; s++) {
            if (*s == '\\' && s[1]) {
                s++;
                if (*s >= '0' && *s <= '7') {
                    int v = 0;
                    for (int k = 0; k < 3 && *s >= '0' && *s <= '7'; k++, s++)
                        v = v * 8 + (*s - '0');
                    s--;
                    r[n++] = (char)v;
                    continue;
                }
                const char *from = "abfnrtv", *to = "\a\b\f\n\r\t\v";
                const char *k = strchr(from, *s);
                r[n++] = k ? to[k - from] : *s;
                continue;
            }
            r[n++] = *s;
        }
    } else {
        const char *end = strchr(s, '\t');
        if (!end)
            end = s + strcspn(s, " ");
        memcpy(r, s, (size_t)(end - s));
        n = (size_t)(end - s);
    }
    r[n] = '\0';
    return r;
}

/* the name with -p num components taken off (none: the basename) */
static char *strip(const char *name)
{
    if (!name || !*name)
        return NULL;
    if (opt_p < 0) {
        const char *b = strrchr(name, '/');
        return xstrdup(b ? b + 1 : name);
    }
    const char *s = name;
    for (int k = 0; k < opt_p; k++) {
        const char *sl = strchr(s, '/');
        if (!sl)
            return NULL;
        while (*sl == '/')
            sl++;
        s = sl;
    }
    return *s ? xstrdup(s) : NULL;
}

static bool exists(const char *name)
{
    struct stat st;
    return name && stat(name, &st) == 0;
}

/* get -e SCCS/s.name (XSI): true if the file is there after it */
static bool sccs_get(const char *name)
{
    if (!name)
        return false;
    char *d = xstrdup(name), *b = strrchr(d, '/');
    char path[PATH_MAX];
    if (b) {
        *b = '\0';
        snprintf(path, sizeof path, "%s/SCCS/s.%s", d, b + 1);
    } else {
        snprintf(path, sizeof path, "SCCS/s.%s", d);
    }
    free(d);
    if (!exists(path))
        return false;
    pid_t pid = fork();
    if (pid == 0) {
        execlp("get", "get", "-e", path, (char *)NULL);
        _exit(127);
    }
    int st;
    if (pid < 0 || waitpid(pid, &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st))
        return false;
    return exists(name);
}

static FILE *tty_open(void)
{
    return fopen("/dev/tty", "r+");
}

/* ---- the files being patched ---- */

/* the current versions of the files, with -o */
struct version {
    char *name;
    struct text t;
    struct version *next;
};

static struct version *versions;
static char **backed_up;
static size_t nbacked;
static FILE *out_file;          /* -o */
static FILE *rej_all;           /* -r */

static bool was_backed_up(const char *name)
{
    for (size_t i = 0; i < nbacked; i++)
        if (strcmp(backed_up[i], name) == 0)
            return true;
    backed_up = xrealloc(backed_up, (nbacked + 1) * sizeof *backed_up);
    backed_up[nbacked++] = xstrdup(name);
    return false;
}

static bool write_lines(FILE *f, const struct text *t)
{
    for (size_t i = 0; i < t->n; i++) {
        fputs(t->l[i].s, f);
        if (!(i == t->n - 1 && t->l[i].noeol))
            putc('\n', f);
    }
    return !ferror(f);
}

static bool copy_file(const char *from, const char *to)
{
    FILE *a = fopen(from, "r");
    if (!a)
        return false;
    FILE *b = fopen(to, "w");
    if (!b) {
        fclose(a);
        return false;
    }
    char buf[8192];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof buf, a)) > 0)
        if (fwrite(buf, 1, n, b) != n)
            ok = false;
    fclose(a);
    return fclose(b) == 0 && ok;
}

/* write t to name: a new file next to it, then renamed */
static bool replace_file(const char *name, const struct text *t)
{
    struct stat st;
    mode_t mode = 0666 & ~(mode_t)0;
    bool had = stat(name, &st) == 0;
    if (had)
        mode = st.st_mode & 07777;
    char *tmp = xmalloc(strlen(name) + 16);
    sprintf(tmp, "%s.patchXXXXXX", name);
    int fd = mkstemp(tmp);
    if (fd < 0) {
        fprintf(stderr, "patch: %s: %s\n", name, strerror(errno));
        free(tmp);
        return false;
    }
    if (had)
        fchmod(fd, mode);
    else {
        mode_t m = umask(0);
        umask(m);
        fchmod(fd, 0666 & ~m);
    }
    FILE *f = fdopen(fd, "w");
    bool ok = f && write_lines(f, t);
    if (f && fclose(f) != 0)
        ok = false;
    if (!ok || rename(tmp, name) != 0) {
        fprintf(stderr, "patch: %s: %s\n", name, strerror(errno));
        unlink(tmp);
        free(tmp);
        return false;
    }
    free(tmp);
    return true;
}

/* ---- applying hunks ---- */

static bool blank_eq(const char *a, const char *b)
{
    for (;;) {
        if (isblank((unsigned char)*a) && isblank((unsigned char)*b)) {
            while (isblank((unsigned char)*a))
                a++;
            while (isblank((unsigned char)*b))
                b++;
            continue;
        }
        if (*a != *b)
            return false;
        if (!*a)
            return true;
        a++, b++;
    }
}

static bool line_eq(const char *a, const char *b)
{
    return opt_l ? blank_eq(a, b) : strcmp(a, b) == 0;
}

/* do the old lines of h, less skip_front context lines at the front and
   skip_back at the back, match t at pos? */
static bool matches(const struct text *t, const struct hunk *h, size_t pos,
                    size_t skip_front, size_t skip_back)
{
    size_t k = pos;
    for (size_t i = skip_front; i + skip_back < h->n; i++) {
        if (h->l[i].type == '+')
            continue;
        if (k >= t->n || !line_eq(t->l[k].s, h->l[i].s))
            return false;
        k++;
    }
    return true;
}

static size_t count_old(const struct hunk *h, size_t from, size_t to)
{
    size_t n = 0;
    for (size_t i = from; i < to; i++)
        if (h->l[i].type != '+')
            n++;
    return n;
}

/* the context lines at the front and the back of h */
static void context_ends(const struct hunk *h, size_t *front, size_t *back)
{
    *front = *back = 0;
    while (*front < h->n && h->l[*front].type == ' ')
        (*front)++;
    while (*back < h->n - *front && h->l[h->n - 1 - *back].type == ' ')
        (*back)++;
}

/* find where h goes in t: true with *at (the index in t of the first
   old line) and *fuzz.  As GNU patch does: a hunk with less context at
   one end than at the other is at that end of the file; at fuzz f, the
   end with the most context loses f lines of it, the other end f less
   what it lacks */
static bool locate(const struct text *t, const struct hunk *h, long expect, size_t from,
                   size_t *at, int *fuzz)
{
    size_t front, back;
    context_ends(h, &front, &back);
    long context = (long)(front > back ? front : back);
    int maxfuzz = h->kind == 'c' || h->kind == 'u' ? 2 : 0;
    size_t total = count_old(h, 0, h->n);
    long last_skf = -1, last_skb = -1;
    for (int f = 0; f <= maxfuzz; f++) {
        long pfz = f + (long)front - context, sfz = f + (long)back - context;
        size_t skf = pfz > 0 ? ((size_t)pfz < front ? (size_t)pfz : front) : 0;
        size_t skb = sfz > 0 ? ((size_t)sfz < back ? (size_t)sfz : back) : 0;
        if ((long)skf == last_skf && (long)skb == last_skb && !(pfz < 0 || sfz < 0))
            continue;
        last_skf = (long)skf, last_skb = (long)skb;
        if (pfz < 0 || sfz < 0) {
            /* at the start, the end, or the whole of the file */
            size_t p;
            if (pfz < 0 && sfz < 0 && total != t->n)
                continue;
            if (pfz < 0)
                p = 0;
            else if (total <= t->n)
                p = t->n - total;
            else
                continue;
            if (p < from || p + total > t->n)
                continue;
            if (matches(t, h, p + skf, pfz < 0 ? 0 : skf, sfz < 0 ? 0 : skb)) {
                *at = p;
                *fuzz = f;
                return true;
            }
            continue;
        }
        /* the place of the hunk, then outwards from it */
        /* the lines skipped at the back may be past the end */
        long lo = (long)from, hi = (long)t->n - (long)count_old(h, 0, h->n - skb);
        for (long d = 0; expect - d >= lo || expect + d <= hi; d++) {
            long cand[2] = { expect - d, expect + d };
            for (int k = 0; k < (d ? 2 : 1); k++) {
                long p = cand[k];
                if (p < lo || p > hi)
                    continue;
                if (matches(t, h, (size_t)p + skf, skf, skb)) {
                    *at = (size_t)p;
                    *fuzz = f;
                    return true;
                }
            }
        }
    }
    return false;
}

/* t with h applied at index at: into r */
static void apply(const struct text *t, const struct hunk *h, size_t at, struct text *r)
{
    for (size_t i = 0; i < at; i++)
        add_line(r, t->l[i].s, strlen(t->l[i].s), t->l[i].noeol);
    size_t k = at;
    for (size_t i = 0; i < h->n; ) {
        const struct hline *x = &h->l[i];
        if (x->type == ' ') {
            /* the file's line, with the newline as the patch has it */
            if (k < t->n)
                add_line(r, t->l[k].s, strlen(t->l[k].s), x->noeol);
            k++, i++;
            continue;
        }
        if (!opt_D) {
            if (x->type == '-')
                k++;
            else
                add_line(r, x->s, strlen(x->s), x->noeol);
            i++;
            continue;
        }
        /* -D: a group of - and + lines */
        size_t j = i, dels = 0, adds = 0;
        while (j < h->n && h->l[j].type != ' ')
            if (h->l[j++].type == '-')
                dels++;
            else
                adds++;
        char mark[PATH_MAX + 16];
        if (dels) {
            snprintf(mark, sizeof mark, "#ifndef %s", opt_D);
            add_line(r, mark, strlen(mark), false);
            for (size_t q = i; q < j; q++)
                if (h->l[q].type == '-') {
                    add_line(r, t->l[k].s, strlen(t->l[k].s), false);
                    k++;
                }
            if (adds) {
                add_line(r, "#else", 5, false);
            }
        } else {
            snprintf(mark, sizeof mark, "#ifdef %s", opt_D);
            add_line(r, mark, strlen(mark), false);
        }
        for (size_t q = i; q < j; q++)
            if (h->l[q].type == '+')
                add_line(r, h->l[q].s, strlen(h->l[q].s), false);
        add_line(r, "#endif", 6, false);
        i = j;
    }
    for (size_t i = k; i < t->n; i++)
        add_line(r, t->l[i].s, strlen(t->l[i].s), t->l[i].noeol);
    /* a newline after every line but the last */
    for (size_t i = 0; i + 1 < r->n; i++)
        r->l[i].noeol = false;
}

/* ---- the reject file ---- */

static void write_reject(FILE *f, const struct hunk *h, long at)
{
    long olds = (long)count_old(h, 0, h->n), news = 0;
    for (size_t i = 0; i < h->n; i++)
        if (h->l[i].type != '-')
            news++;
    long ostart = at, nstart = at;
    if (h->kind == 'u') {
        fprintf(f, "@@ -%ld", olds ? ostart : ostart - 1);
        if (olds != 1)
            fprintf(f, ",%ld", olds);
        fprintf(f, " +%ld", news ? nstart : nstart - 1);
        if (news != 1)
            fprintf(f, ",%ld", news);
        fprintf(f, " @@\n");
        for (size_t i = 0; i < h->n; i++) {
            fprintf(f, "%c%s\n", h->l[i].type, h->l[i].s);
            if (h->l[i].noeol)
                fprintf(f, "\\ No newline at end of file\n");
        }
        return;
    }
    fprintf(f, "***************\n");
    if (olds == 0)
        fprintf(f, "*** %ld ****\n", ostart - 1);
    else if (olds == 1)
        fprintf(f, "*** %ld ****\n", ostart);
    else
        fprintf(f, "*** %ld,%ld ****\n", ostart, ostart + olds - 1);
    bool changes = false;
    for (size_t i = 0; i < h->n; i++)
        if (h->l[i].type == '-')
            changes = true;
    /* "!" for - lines next to + lines, as diff -c writes */
    for (size_t i = 0; i < h->n && changes; i++) {
        const struct hline *x = &h->l[i];
        if (x->type == '+')
            continue;
        char m = ' ';
        if (x->type == '-') {
            size_t j = i;
            while (j < h->n && h->l[j].type == '-')
                j++;
            bool bang = j < h->n && h->l[j].type == '+';
            size_t b = i;
            while (b > 0 && h->l[b - 1].type == '-')
                b--;
            if (b > 0 && h->l[b - 1].type == '+')
                bang = true;
            m = bang ? '!' : '-';
        }
        fprintf(f, "%c %s\n", m, x->s);
        if (x->noeol)
            fprintf(f, "\\ No newline at end of file\n");
    }
    if (news == 0)
        fprintf(f, "--- %ld ----\n", nstart - 1);
    else if (news == 1)
        fprintf(f, "--- %ld ----\n", nstart);
    else
        fprintf(f, "--- %ld,%ld ----\n", nstart, nstart + news - 1);
    bool adds = false;
    for (size_t i = 0; i < h->n; i++)
        if (h->l[i].type == '+')
            adds = true;
    for (size_t i = 0; i < h->n && adds; i++) {
        const struct hline *x = &h->l[i];
        if (x->type == '-')
            continue;
        char m = ' ';
        if (x->type == '+') {
            size_t j = i;
            while (j < h->n && h->l[j].type == '+')
                j++;
            bool bang = j < h->n && h->l[j].type == '-';
            size_t b = i;
            while (b > 0 && h->l[b - 1].type == '+')
                b--;
            if (b > 0 && h->l[b - 1].type == '-')
                bang = true;
            m = bang ? '!' : '+';
        }
        fprintf(f, "%c %s\n", m, x->s);
        if (x->noeol)
            fprintf(f, "\\ No newline at end of file\n");
    }
}

/* ---- ed scripts ---- */

static long ed_line;            /* the current line, 1-based */

static bool ed_subst(struct text *t, long a, long b, const char *cmd)
{
    /* s/re/replacement/[g], the delimiter after s */
    char delim = cmd[0];
    if (!delim || delim == ' ' || delim == '\n')
        return false;
    const char *p = cmd + 1;
    char re[1024], rep[1024];
    size_t n = 0;
    while (*p && *p != delim && n < sizeof re - 1) {
        if (*p == '\\' && p[1] == delim)
            p++;
        re[n++] = *p++;
    }
    re[n] = '\0';
    if (*p != delim)
        return false;
    p++;
    n = 0;
    while (*p && *p != delim && n < sizeof rep - 1) {
        if (*p == '\\' && p[1])
            rep[n++] = *p++;
        rep[n++] = *p++;
    }
    rep[n] = '\0';
    bool g = *p == delim && p[1] == 'g';
    regex_t rx;
    if (regcomp(&rx, re, 0) != 0)
        return false;
    for (long i = a; i <= b; i++) {
        struct line *l = &t->l[i - 1];
        char out[8192];
        size_t on = 0;
        const char *s = l->s;
        regmatch_t m;
        int flags = 0;
        while (regexec(&rx, s, 1, &m, flags) == 0) {
            for (const char *q = s; q < s + m.rm_so && on < sizeof out - 1; q++)
                out[on++] = *q;
            for (const char *q = rep; *q && on < sizeof out - 1; q++) {
                if (*q == '&') {
                    for (const char *x = s + m.rm_so; x < s + m.rm_eo && on < sizeof out - 1; x++)
                        out[on++] = *x;
                } else {
                    if (*q == '\\' && q[1])
                        q++;
                    out[on++] = *q;
                }
            }
            s += m.rm_eo;
            if (!g || m.rm_eo == m.rm_so)
                break;
            flags = REG_NOTBOL;
        }
        while (*s && on < sizeof out - 1)
            out[on++] = *s++;
        out[on] = '\0';
        free(l->s);
        l->s = xstrdup(out);
    }
    regfree(&rx);
    ed_line = b;
    return true;
}

/* run the ed commands from pl on t; false on a bad command */
static bool run_ed(struct text *t)
{
    ed_line = (long)t->n;
    for (;;) {
        const char *s = P(pl);
        long a, b, c, d;
        char cmd;
        if (!s)
            break;
        if (strcmp(s, "w") == 0 || strcmp(s, "q") == 0 || strcmp(s, "wq") == 0) {
            pl++;
            continue;
        }
        /* s/.../ on the current line */
        if (s[0] == 's' && !isalnum((unsigned char)s[1])) {
            pl++;
            if (ed_line < 1 || !ed_subst(t, ed_line, ed_line, s + 1))
                return false;
            continue;
        }
        if (!is_command(s, true, &a, &b, &cmd, &c, &d))
            break;
        pl++;
        if (a < 0 || b < a || b > (long)t->n || (a == 0 && cmd != 'a'))
            return false;
        if (cmd == 's') {
            const char *x = strchr(s, 's');
            if (!ed_subst(t, a, b, x + 1))
                return false;
            continue;
        }
        if (cmd == 'd' || cmd == 'c') {
            for (long i = a; i <= b; i++)
                free(t->l[i - 1].s);
            memmove(t->l + a - 1, t->l + b, (t->n - (size_t)b) * sizeof *t->l);
            t->n -= (size_t)(b - a + 1);
            ed_line = a - 1;
            if (cmd == 'd') {
                if (ed_line < (long)t->n)
                    ed_line++;
                continue;
            }
        }
        long after = cmd == 'a' ? b : cmd == 'i' ? a - 1 : a - 1;
        if (cmd == 'i' && after < 0)
            after = 0;
        /* the text, up to a line "." */
        struct text in = { 0 };
        for (;;) {
            const char *x = P(pl);
            if (!x)
                return false;
            pl++;
            if (strcmp(x, ".") == 0)
                break;
            add_line(&in, x, strlen(x), false);
        }
        t->l = xrealloc(t->l, (t->n + in.n + 1) * sizeof *t->l);
        t->cap = t->n + in.n + 1;
        memmove(t->l + after + in.n, t->l + after, (t->n - (size_t)after) * sizeof *t->l);
        memcpy(t->l + after, in.l, in.n * sizeof *in.l);
        t->n += in.n;
        free(in.l);
        ed_line = after + (long)in.n;
    }
    for (size_t i = 0; i + 1 < t->n; i++)
        t->l[i].noeol = false;
    return true;
}

/* ---- one patch ---- */

static bool applied_any;        /* a hunk applied, in the whole run */

static bool ask_yes(const char *prompt, bool dflt)
{
    FILE *tty = tty_open();
    if (!tty)
        return false;
    fprintf(tty, "%s", prompt);
    fflush(tty);
    char buf[256];
    bool r = dflt;
    if (fgets(buf, sizeof buf, tty)) {
        buf[strcspn(buf, "\n")] = '\0';
        if (buf[0]) {
            const char *y = nl_langinfo(YESEXPR);
            regex_t re;
            if (y && *y && regcomp(&re, y, REG_EXTENDED | REG_NOSUB) == 0) {
                r = regexec(&re, buf, 0, NULL, 0) == 0;
                regfree(&re);
            } else {
                r = buf[0] == 'y' || buf[0] == 'Y';
            }
        }
    } else {
        r = false;
    }
    fclose(tty);
    return r;
}

/* the hunks of a patch from pl, in kind k, on the file name */
static void do_patch(char kind, const char *name, bool creating, bool deleting)
{
    struct text t = { 0 };
    struct version *v = NULL;
    if (opt_o) {
        for (v = versions; v; v = v->next)
            if (strcmp(v->name, name) == 0)
                break;
    }
    if (v) {
        for (size_t i = 0; i < v->t.n; i++)
            add_line(&t, v->t.l[i].s, strlen(v->t.l[i].s), v->t.l[i].noeol);
    } else {
        FILE *f = fopen(name, "r");
        if (!f && !(creating && errno == ENOENT)) {
            fprintf(stderr, "patch: %s: %s\n", name, strerror(errno));
            set_status(2);
            /* skip the hunks */
            while (pl < pf.n && hunk_kind(pl) == kind) {
                struct hunk h = { 0 };
                if (kind == 'u') parse_unified(&h);
                else if (kind == 'c') parse_context(&h);
                else if (kind == 'n') parse_normal(&h);
                else { run_ed(&t); free_text(&t); break; }
                free_hunk(&h);
            }
            return;
        }
        if (f) {
            if (!read_text(f, &t)) {
                fprintf(stderr, "patch: %s: %s\n", name, strerror(errno));
                set_status(2);
            }
            fclose(f);
        }
    }
    fprintf(stderr, "patching file %s\n", name);

    if (opt_b && !opt_o && !was_backed_up(name)) {
        char *orig = xmalloc(strlen(name) + 6);
        sprintf(orig, "%s.orig", name);
        if (exists(name) ? !copy_file(name, orig) : (fclose(fopen(orig, "w")), false)) {
            fprintf(stderr, "patch: %s: %s\n", orig, strerror(errno));
            set_status(2);
        }
        free(orig);
    }

    FILE *rej = NULL;
    char *rejname = NULL;
    int nhunks = 0, failed = 0;
    long offset = 0, delta = 0;     /* delta: lines added by the hunks before */
    size_t from = 0;            /* hunks go after the hunk before */
    bool ignore = false;        /* -N and the patch is applied already */
    bool asked = false;         /* about -R, and the answer was no */
    if (kind == 'e') {
        if (opt_R) {
            fprintf(stderr, "patch: -R cannot be used with an ed script\n");
            set_status(2);
            run_ed(&(struct text){ 0 });
        } else if (!run_ed(&t)) {
            fprintf(stderr, "patch: %s: the ed script does not fit\n", name);
            set_status(2);
            goto done;
        }
        nhunks = 1;
    }
    while (kind != 'e' && pl < pf.n && hunk_kind(pl) == kind) {
        struct hunk h = { 0 };
        if (kind == 'u')
            parse_unified(&h);
        else if (kind == 'c')
            parse_context(&h);
        else
            parse_normal(&h);
        nhunks++;
        if (ignore) {
            free_hunk(&h);
            continue;
        }
        if (opt_R)
            reverse(&h);
        /* the first old line, as an index; after line n when there are no
           old lines */
        long expect = (h.old_n == 0 ? h.old_start : h.old_start - 1) + delta + offset;
        size_t at;
        int fuzz;
        bool ok = locate(&t, &h, expect, from, &at, &fuzz);
        if (!ok && !opt_R && !applied_any && !asked) {
            /* reversed, or applied already? */
            reverse(&h);
            long rexpect = (h.old_n == 0 ? h.old_start : h.old_start - 1) + delta + offset;
            size_t rat;
            int rfuzz;
            bool rok = h.old_n > 0 || h.new_n > 0 ? locate(&t, &h, rexpect, from, &rat, &rfuzz) : false;
            /* an insertion fits anywhere: look for its lines instead */
            if (rok && count_old(&h, 0, h.n) == 0)
                rok = false;
            if (rok) {
                if (opt_N) {
                    fprintf(stderr, "Reversed (or previously applied) patch detected!  Skipping patch.\n");
                    ignore = true;
                    free_hunk(&h);
                    continue;
                }
                if (ask_yes("Reversed (or previously applied) patch detected!  Assume -R? [y] ", true)) {
                    opt_R = true;
                    ok = true, at = rat, fuzz = rfuzz, expect = rexpect;
                } else {
                    reverse(&h);
                    asked = true;
                }
            } else {
                reverse(&h);
            }
        }
        if (ok) {
            struct text r = { 0 };
            apply(&t, &h, at, &r);
            free_text(&t);
            t = r;
            long moved = (long)at - expect;
            if (moved || fuzz) {
                fprintf(stderr, "Hunk #%d succeeded at %ld", nhunks, (long)at + 1);
                if (fuzz)
                    fprintf(stderr, " with fuzz %d", fuzz);
                if (moved)
                    fprintf(stderr, " (offset %ld line%s)", moved, moved == 1 || moved == -1 ? "" : "s");
                fprintf(stderr, ".\n");
            }
            offset += moved;
            for (size_t i = 0; i < h.n; i++)
                delta += h.l[i].type == '+' ? 1 : h.l[i].type == '-' ? -1 : 0;
            long newn = 0;
            for (size_t i = 0; i < h.n; i++)
                if (h.l[i].type != '-')
                    newn++;
            /* the next hunk may use this one's trailing context */
            size_t upto = 0, last = 0;
            for (size_t i = 0; i < h.n; i++) {
                if (h.l[i].type != '-')
                    upto++;
                if (h.l[i].type != ' ')
                    last = upto;
            }
            from = at + last;
            (void)newn;
            applied_any = true;
        } else {
            failed++;
            fprintf(stderr, "Hunk #%d FAILED at %ld.\n", nhunks, expect + 1);
            if (!rej) {
                if (opt_r) {
                    if (!rej_all)
                        rej_all = fopen(opt_r, "w");
                    rej = rej_all;
                    rejname = xstrdup(opt_r);
                } else {
                    rejname = xmalloc(strlen(opt_o ? opt_o : name) + 5);
                    sprintf(rejname, "%s.rej", opt_o ? opt_o : name);
                    rej = fopen(rejname, "w");
                }
                if (!rej) {
                    fprintf(stderr, "patch: %s: %s\n", rejname, strerror(errno));
                    set_status(2);
                } else if (h.kind == 'u') {
                    fprintf(rej, "--- %s\n+++ %s\n", name, name);
                } else {
                    fprintf(rej, "*** %s\n--- %s\n", name, name);
                }
            }
            if (rej)
                write_reject(rej, &h, expect + 1);
        }
        free_hunk(&h);
    }
    if (failed) {
        fprintf(stderr, "%d out of %d hunk%s FAILED -- saving rejects to file %s\n",
                failed, nhunks, nhunks == 1 ? "" : "s", rejname ? rejname : "?");
        set_status(1);
    }
    if (rej && rej != rej_all)
        fclose(rej);
    free(rejname);
    if (ignore)
        goto done;

    /* write the result */
    if (opt_o) {
        if (!v) {
            v = calloc(1, sizeof *v);
            v->name = xstrdup(name);
            v->next = versions;
            versions = v;
        }
        free_text(&v->t);
        for (size_t i = 0; i < t.n; i++)
            add_line(&v->t, t.l[i].s, strlen(t.l[i].s), t.l[i].noeol);
        if (!write_lines(out_file, &t)) {
            fprintf(stderr, "patch: %s: %s\n", opt_o, strerror(errno));
            set_status(2);
        }
    } else if (deleting && t.n == 0) {
        if (unlink(name) != 0) {
            fprintf(stderr, "patch: %s: %s\n", name, strerror(errno));
            set_status(2);
        }
    } else if (!replace_file(name, &t)) {
        set_status(2);
    }
done:
    free_text(&t);
}

/* ---- the patches of the file ---- */

static bool is_dev_null(const char *s)
{
    return s && strcmp(s, "/dev/null") == 0;
}

static void run(void)
{
    while (pl < pf.n) {
        /* the header: up to the first hunk */
        char *index = NULL, *old = NULL, *new = NULL;
        char *ctx_star = NULL, *ctx_dash = NULL;
        char kind = 0;
        free(indent);
        indent = NULL;
        indent_len = 0;
        while (pl < pf.n) {
            const char *raw = pf.l[pl].s;
            /* the indent of this line, tried for a hunk */
            size_t ind = strspn(raw, " \t");
            char *save = indent;
            size_t save_len = indent_len;
            indent = xstrndup(raw, ind);
            indent_len = ind;
            char k = hunk_kind(pl);
            if (fmt_opt && k != fmt_opt)
                k = 0;
            if (k) {
                free(save);
                kind = k;
                break;
            }
            free(indent);
            indent = save;
            indent_len = save_len;
            const char *s = raw + ind;
            if (strncmp(s, "Index:", 6) == 0) {
                free(index);
                const char *n = s + 6;
                while (*n == ' ' || *n == '\t')
                    n++;
                index = xstrdup(n);
                index[strcspn(index, "\t")] = '\0';
                size_t l = strlen(index);
                while (l > 0 && index[l - 1] == ' ')
                    index[--l] = '\0';
            } else if (strncmp(s, "*** ", 4) == 0) {
                free(ctx_star);
                ctx_star = header_name(s + 4);
            } else if (strncmp(s, "--- ", 4) == 0) {
                free(ctx_dash);
                ctx_dash = header_name(s + 4);
            } else if (strncmp(s, "+++ ", 4) == 0) {
                free(new);
                new = header_name(s + 4);
            }
            pl++;
        }
        if (!kind)
            break;
        /* the names of the kind of hunk */
        if (kind == 'u') {
            old = ctx_dash, ctx_dash = NULL;
        } else if (kind == 'c') {
            old = ctx_star, ctx_star = NULL;
            free(new);
            new = ctx_dash, ctx_dash = NULL;
        }
        char *name = NULL;
        bool creating = false, deleting = false;
        if (operand) {
            name = xstrdup(operand);
        } else {
            char *o = is_dev_null(old) ? NULL : strip(old);
            char *n = is_dev_null(new) ? NULL : strip(new);
            char *x = strip(index);
            deleting = is_dev_null(new);
            if (exists(o))
                name = o, o = NULL;
            else if (exists(n))
                name = n, n = NULL;
            else if (exists(x))
                name = x, x = NULL;
            else if (sccs_get(o ? o : n ? n : x))
                name = xstrdup(o ? o : n ? n : x);
            /* a new file: the old side empty */
            if (!name && (is_dev_null(old) || kind == 'u' || kind == 'c')) {
                size_t save = pl;
                bool empty_old = true;
                char k2 = kind;
                while (pl < pf.n && hunk_kind(pl) == k2) {
                    struct hunk h = { 0 };
                    if (k2 == 'u') parse_unified(&h); else if (k2 == 'c') parse_context(&h); else break;
                    if (count_old(&h, 0, h.n) > 0)
                        empty_old = false;
                    free_hunk(&h);
                }
                pl = save;
                if (empty_old && (n || o || x)) {
                    name = xstrdup(n ? n : o ? o : x);
                    creating = true;
                }
            }
            free(o);
            free(n);
            free(x);
            if (!name) {
                FILE *tty = tty_open();
                if (tty) {
                    printf("File to patch: ");
                    fflush(stdout);
                    char buf[PATH_MAX];
                    if (fgets(buf, sizeof buf, tty)) {
                        buf[strcspn(buf, "\n")] = '\0';
                        if (buf[0])
                            name = xstrdup(buf);
                    }
                    fclose(tty);
                }
            }
        }
        if (name) {
            do_patch(kind, name, creating, deleting);
        } else {
            fprintf(stderr, "patch: cannot find the file to patch at line %zu: skipping the patch\n", pl + 1);
            set_status(2);
            while (pl < pf.n && hunk_kind(pl) == kind) {
                struct hunk h = { 0 };
                struct text dummy = { 0 };
                if (kind == 'u') parse_unified(&h);
                else if (kind == 'c') parse_context(&h);
                else if (kind == 'n') parse_normal(&h);
                else { run_ed(&dummy); free_text(&dummy); break; }
                free_hunk(&h);
            }
        }
        free(name);
        free(index);
        free(old);
        free(new);
        free(ctx_star);
        free(ctx_dash);
    }
}

static void usage(void)
{
    fprintf(stderr, "usage: patch [-blNR] [-c|-e|-n|-u] [-d dir] [-D define] [-i patchfile]\n"
                    "             [-o outfile] [-p num] [-r rejectfile] [file]\n");
    exit(2);
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    const char *dir = NULL, *input = NULL;
    int c;
    while ((c = getopt(argc, argv, "bcd:D:ei:lNno:p:r:Ru")) != -1) {
        switch (c) {
        case 'b': opt_b = true; break;
        case 'c': case 'e': case 'n': case 'u':
            if (fmt_opt && fmt_opt != c)
                usage();
            fmt_opt = (char)c;
            break;
        case 'd': dir = optarg; break;
        case 'D': opt_D = optarg; break;
        case 'i': input = optarg; break;
        case 'l': opt_l = true; break;
        case 'N': opt_N = true; break;
        case 'o': opt_o = optarg; break;
        case 'p': {
            char *e;
            long v = strtol(optarg, &e, 10);
            if (*optarg == '\0' || *e || v < 0 || v > INT_MAX)
                usage();
            opt_p = (int)v;
            break;
        }
        case 'r': opt_r = optarg; break;
        case 'R': opt_R = true; break;
        default: usage();
        }
    }
    if (argc - optind > 1)
        usage();
    if (optind < argc)
        operand = argv[optind];
    if (opt_R && fmt_opt == 'e') {
        fprintf(stderr, "patch: -R cannot be used with ed scripts\n");
        return 2;
    }
    if (dir && chdir(dir) != 0) {
        fprintf(stderr, "patch: %s: %s\n", dir, strerror(errno));
        return 2;
    }
    FILE *in = stdin;
    if (input && !(in = fopen(input, "r"))) {
        fprintf(stderr, "patch: %s: %s\n", input, strerror(errno));
        return 2;
    }
    if (!read_text(in, &pf)) {
        fprintf(stderr, "patch: %s: %s\n", input ? input : "standard input", strerror(errno));
        return 2;
    }
    if (in != stdin)
        fclose(in);
    if (opt_o) {
        if (opt_b && exists(opt_o)) {
            char *orig = xmalloc(strlen(opt_o) + 6);
            sprintf(orig, "%s.orig", opt_o);
            if (!copy_file(opt_o, orig)) {
                fprintf(stderr, "patch: %s: %s\n", orig, strerror(errno));
                set_status(2);
            }
            free(orig);
        }
        if (!(out_file = fopen(opt_o, "w"))) {
            fprintf(stderr, "patch: %s: %s\n", opt_o, strerror(errno));
            return 2;
        }
    }
    run();
    if (out_file && fclose(out_file) != 0) {
        fprintf(stderr, "patch: %s: %s\n", opt_o, strerror(errno));
        set_status(2);
    }
    if (rej_all && fclose(rej_all) != 0)
        set_status(2);
    return status;
}
