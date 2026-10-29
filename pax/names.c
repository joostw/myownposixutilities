/*
 * names.c - selection of archive members (pattern operands, -c, -n, -u),
 * -s substitutions, -i interactive renaming and the mapping of renamed
 * link targets.
 */
#include "pax.h"

char **patterns;
int npatterns;
static int *pat_used;
static char **pat_dir;      /* -n: the directory matched by the pattern */

/* does the pattern match the member name or (without -d) a parent of it? */
static int pattern_matches(const char *pat, const char *name)
{
    size_t l = strlen(name);
    char *n = xstrdup(name);
    int r = 0;
    while (l > 1 && n[l - 1] == '/')
        n[--l] = 0;
    if (fnmatch(pat, n, FNM_PATHNAME | FNM_PERIOD) == 0)
        r = 1;
    else if (!opt_d) {
        for (size_t i = 1; i < l && !r; i++) {
            if (n[i] != '/')
                continue;
            n[i] = 0;
            if (fnmatch(pat, n, FNM_PATHNAME | FNM_PERIOD) == 0)
                r = 1;
            n[i] = '/';
        }
    }
    free(n);
    return r;
}

static int under_dir(const char *dir, const char *name)
{
    size_t l = strlen(dir);
    while (l > 1 && dir[l - 1] == '/')
        l--;
    return strncmp(dir, name, l) == 0 && (name[l] == '/' || name[l] == 0);
}

/* -u in read and list modes: is the member newer than the file? */
int newer_than_existing(Entry *e, const char *path)
{
    struct stat st;
    if (lstat(path, &st) < 0)
        return 1;
    if (e->mtime.tv_sec != st.st_mtim.tv_sec)
        return e->mtime.tv_sec > st.st_mtim.tv_sec;
    return e->mtime.tv_nsec > st.st_mtim.tv_nsec;
}

int select_member(Entry *e)
{
    int sel = 0, which = -1;
    if (!pat_used && npatterns) {
        pat_used = calloc((size_t)npatterns, sizeof(int));
        pat_dir = calloc((size_t)npatterns, sizeof(char *));
    }
    if (npatterns == 0)
        sel = 1;
    else {
        for (int i = 0; i < npatterns; i++) {
            if (opt_n && pat_used[i]) {
                /* only the hierarchy below the directory it matched */
                if (pat_dir[i] && !opt_d && under_dir(pat_dir[i], e->path)) {
                    sel = 1;
                    which = -2;
                    break;
                }
                continue;
            }
            if (pattern_matches(patterns[i], e->path)) {
                sel = 1;
                which = i;
                break;
            }
        }
    }
    if (opt_c)
        sel = !sel;
    if (sel && opt_u && opt_r && !opt_w && !newer_than_existing(e, e->path))
        return 0;
    if (sel && which >= 0) {
        pat_used[which] = 1;
        if (opt_n && e->type == T_DIR)
            pat_dir[which] = xstrdup(e->path);
    }
    if (!sel && opt_c && which >= 0)
        pat_used[which] = 1;
    return sel;
}

int all_patterns_done(void)
{
    /* with -n, keep reading while a matched directory may have more members */
    for (int i = 0; i < npatterns; i++)
        if (!pat_used || !pat_used[i] || (pat_dir[i] && !opt_d))
            return 0;
    return 1;
}

void report_unmatched(void)
{
    for (int i = 0; i < npatterns; i++)
        if (!pat_used || !pat_used[i])
            warnx_("%s: not found in archive", patterns[i]);
}

/* ---------- -s ---------- */

typedef struct Subst {
    regex_t re;
    char *repl;
    int g, p, s, S;
} Subst;
static Subst *substs;
static int nsub;

int nsubsts(void) { return nsub; }

/* the part of a -s argument up to the unescaped delimiter */
static char *scan_part(const char **pp, int delim, int keep_escape)
{
    const char *p = *pp;
    Buf b = { 0 };
    while (*p && (unsigned char)*p != delim) {
        if (*p == '\\' && p[1]) {
            if ((unsigned char)p[1] == delim) {
                buf_addc(&b, p[1]);
                p += 2;
                continue;
            }
            if (keep_escape)
                buf_addc(&b, '\\');
            buf_addc(&b, p[1]);
            p += 2;
            continue;
        }
        buf_addc(&b, *p++);
    }
    if ((unsigned char)*p != delim)
        return NULL;
    *pp = p + 1;
    return b.s ? b.s : xstrdup("");
}

void add_subst(const char *arg)
{
    int delim = (unsigned char)arg[0];
    const char *p = arg + 1;
    char *old, *new_;
    Subst s;
    int rc;
    if (!delim)
        errx_("-s: missing substitution expression");
    old = scan_part(&p, delim, 1);
    if (!old)
        errx_("-s %s: invalid substitution", arg);
    new_ = scan_part(&p, delim, 1);
    if (!new_)
        errx_("-s %s: invalid substitution", arg);
    memset(&s, 0, sizeof s);
    for (; *p; p++) {
        switch (*p) {
        case 'g':
            s.g = 1;
            break;
        case 'p':
            s.p = 1;
            break;
        case 's':
            s.s = 1;
            break;
        case 'S':
            s.S = 1;
            break;
        default:
            errx_("-s %s: invalid flag %c", arg, *p);
        }
    }
    rc = regcomp(&s.re, old, 0);
    if (rc) {
        char msg[256];
        regerror(rc, &s.re, msg, sizeof msg);
        errx_("-s %s: %s", arg, msg);
    }
    s.repl = new_;
    free(old);
    substs = xrealloc(substs, (size_t)(nsub + 1) * sizeof *substs);
    substs[nsub++] = s;
}

static void add_repl(Buf *b, const char *repl, const char *src, regmatch_t *m)
{
    for (const char *r = repl; *r; r++) {
        if (*r == '&') {
            buf_add(b, src + m[0].rm_so, (size_t)(m[0].rm_eo - m[0].rm_so));
        } else if (*r == '\\' && r[1]) {
            r++;
            if (*r >= '0' && *r <= '9') {
                int k = *r - '0';
                if (m[k].rm_so >= 0)
                    buf_add(b, src + m[k].rm_so, (size_t)(m[k].rm_eo - m[k].rm_so));
            } else if (*r == 'n')
                buf_addc(b, '\n');
            else
                buf_addc(b, *r);
        } else
            buf_addc(b, *r);
    }
}

/* apply the -s expressions; returns 0 if the result is empty */
int apply_substs(char **name, int is_symlink_contents)
{
    for (int i = 0; i < nsub; i++) {
        Subst *s = &substs[i];
        regmatch_t m[10];
        const char *src = *name;
        size_t pos = 0, len = strlen(src);
        Buf b = { 0 };
        int any = 0, eflags = 0;
        if (is_symlink_contents && !s->S)
            continue;
        while (pos <= len) {
            if (regexec(&s->re, src + pos, 10, m, eflags) != 0)
                break;
            for (int k = 0; k < 10; k++)
                if (m[k].rm_so >= 0) {
                    m[k].rm_so += (regoff_t)pos;
                    m[k].rm_eo += (regoff_t)pos;
                }
            buf_add(&b, src + pos, (size_t)m[0].rm_so - pos);
            add_repl(&b, s->repl, src, m);
            any = 1;
            if (m[0].rm_eo == m[0].rm_so) {
                if ((size_t)m[0].rm_eo >= len) {
                    pos = len + 1;
                    break;
                }
                buf_addc(&b, src[m[0].rm_eo]);
                pos = (size_t)m[0].rm_eo + 1;
            } else
                pos = (size_t)m[0].rm_eo;
            eflags = REG_NOTBOL;
            if (!s->g)
                break;
        }
        if (!any) {
            buf_free(&b);
            continue;
        }
        if (pos <= len)
            buf_adds(&b, src + pos);
        if (s->p) {
            fprintf(stderr, "%s >> %s\n", src, b.s ? b.s : "");
            fflush(stderr);
        }
        free(*name);
        *name = b.s ? b.s : xstrdup("");
        return (*name)[0] != 0 || is_symlink_contents;
    }
    return 1;
}

/* ---------- -i ---------- */

static FILE *tty;

int interactive_rename(char **name)
{
    char line[PATH_MAX + 2];
    size_t l;
    if (!tty) {
        tty = fopen("/dev/tty", "r+");
        if (!tty) {
            warn_("/dev/tty");
            exit(1);
        }
    }
    fprintf(tty, "%s: rename? ", *name);
    fflush(tty);
    if (!fgets(line, sizeof line, tty)) {
        warnx_("end of file on /dev/tty");
        exit(1);
    }
    l = strlen(line);
    if (l && line[l - 1] == '\n')
        line[--l] = 0;
    if (l == 0)
        return 0;
    if (strcmp(line, ".") == 0)
        return 1;
    free(*name);
    *name = xstrdup(line);
    return 1;
}

/* ---------- names of renamed members, for hard links ---------- */

typedef struct Ren {
    char *orig, *now;
    struct Ren *next;
} Ren;
static Ren *rentab[4096];

static unsigned hash(const char *s)
{
    unsigned h = 5381;
    while (*s)
        h = h * 33 + (unsigned char)*s++;
    return h % 4096;
}

void note_renamed(const char *orig, const char *now)
{
    unsigned h = hash(orig);
    Ren *r;
    for (r = rentab[h]; r; r = r->next)
        if (strcmp(r->orig, orig) == 0)
            break;
    if (!r) {
        r = xmalloc(sizeof *r);
        r->orig = xstrdup(orig);
        r->next = rentab[h];
        rentab[h] = r;
    } else
        free(r->now);
    r->now = xstrdup(now);
}

char *map_link_name(const char *orig)
{
    for (Ren *r = rentab[hash(orig)]; r; r = r->next)
        if (strcmp(r->orig, orig) == 0)
            return xstrdup(r->now);
    return NULL;
}
