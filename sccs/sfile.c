/*
 * sfile.c - the SCCS file and what the utilities share
 *
 * The format is the classic one (that of AT&T SCCS, Schilling's SCCS and
 * CSSC), so that files can go between them:
 *
 *   ^Ahsum                     the checksum: the bytes after this line
 *   ^As ins/del/unc            for each delta, newest first: statistics,
 *   ^Ad D 1.2 yy/mm/dd hh:mm:ss user serial pred
 *   ^Ai serials ^Ax serials ^Ag serials   included, excluded, ignored
 *   ^Am MR                     one a line
 *   ^Ac comment                one a line
 *   ^Ae
 *   ^Au users ^AU              the users who may make deltas
 *   ^Af x value                the flags
 *   ^At text ^AT               the descriptive text
 *   body                       the weave: ^AI n, ^AD n and ^AE n around
 *                              the lines that delta n inserted or deleted
 *
 * (^A is the character SOH.)  A line of the body is in the text of a
 * version when the innermost ^AI block around it belongs to a delta that
 * is applied, and no ^AD block around it does.
 */

#include "sccs.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

const char *progname = "sccs";
int status;
bool quiet;

/* ---- memory, strings, messages ---- */

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "%s: %s\n", progname, strerror(ENOMEM));
        exit(1);
    }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) {
        fprintf(stderr, "%s: %s\n", progname, strerror(ENOMEM));
        exit(1);
    }
    return p;
}

char *xstrdup(const char *s)
{
    return xstrndup(s, strlen(s));
}

char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *xasprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *p = xmalloc(n > 0 ? (size_t)n + 1 : 1);
    va_start(ap, fmt);
    vsnprintf(p, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return p;
}

void warn(const char *fmt, ...)
{
    va_list ap;
    if (quiet)
        return;
    fflush(stdout);
    fprintf(stderr, "%s: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void err(const char *fmt, ...)
{
    va_list ap;
    status = 1;
    if (quiet)
        return;
    fflush(stdout);
    fprintf(stderr, "%s: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void strs_add(struct strs *l, const char *s)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? 2 * l->cap : 16;
        l->v = xrealloc(l->v, (size_t)l->cap * sizeof *l->v);
    }
    l->v[l->n++] = xstrdup(s);
}

void strs_free(struct strs *l)
{
    for (int i = 0; i < l->n; i++)
        free(l->v[i]);
    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

/* ---- SIDs ---- */

int sid_parse(const char *s, struct sid *sid)
{
    int v[4] = { 0, 0, 0, 0 }, n = 0;
    const char *p = s;
    while (n < 4) {
        if (!isdigit((unsigned char)*p))
            return 0;
        long x = 0;
        while (isdigit((unsigned char)*p)) {
            x = x * 10 + (*p++ - '0');
            if (x > 9999)
                return 0;
        }
        if (x == 0)
            return 0;
        v[n++] = (int)x;
        if (*p == '\0')
            break;
        if (*p++ != '.')
            return 0;
    }
    if (*p)
        return 0;
    sid->r = v[0], sid->l = v[1], sid->b = v[2], sid->s = v[3];
    return n;
}

char *sid_str(const struct sid *sid, char *buf)
{
    if (sid->b)
        snprintf(buf, 64, "%d.%d.%d.%d", sid->r, sid->l, sid->b, sid->s);
    else if (sid->l)
        snprintf(buf, 64, "%d.%d", sid->r, sid->l);
    else
        snprintf(buf, 64, "%d", sid->r);
    return buf;
}

bool sid_eq(const struct sid *a, const struct sid *b)
{
    return a->r == b->r && a->l == b->l && a->b == b->b && a->s == b->s;
}

bool sid_branch(const struct sid *s)
{
    return s->b != 0;
}

/* ---- reading the SCCS file ---- */

static bool ignore_sum;         /* admin -h and -z: the sum is checked apart */

void sccs_ignore_sum(bool on)
{
    ignore_sum = on;
}

static bool int_list(const char *s, int **out, int *n)
{
    *out = NULL;
    *n = 0;
    while (*s) {
        while (*s == ' ')
            s++;
        if (!*s)
            break;
        if (!isdigit((unsigned char)*s))
            return false;
        char *e;
        long v = strtol(s, &e, 10);
        *out = xrealloc(*out, (size_t)(*n + 1) * sizeof **out);
        (*out)[(*n)++] = (int)v;
        s = e;
    }
    return true;
}

/* a line "^Ax..." with the letter x: the text after "^Ax" */
static const char *ctl(const char *line, char c)
{
    if (line[0] != SOH || line[1] != c)
        return NULL;
    return line + 2;
}

/* the flag lines keep their raw text after "^Af x" */
static char *flag_raw[26];

bool sfile_read(const char *path, struct sfile *sf, bool *corrupt)
{
    memset(sf, 0, sizeof *sf);
    if (corrupt)
        *corrupt = false;
    for (int i = 0; i < 26; i++) {
        free(flag_raw[i]);
        flag_raw[i] = NULL;
    }
    sf->path = xstrdup(path);
    FILE *f = fopen(path, "r");
    if (!f) {
        err("%s: %s", path, strerror(errno));
        return false;
    }
    struct strs L = { 0 };
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    unsigned sum = 0;
    bool first = true, noeol = false;
    while ((n = getline(&line, &cap, f)) > 0) {
        if (!first)
            for (ssize_t i = 0; i < n; i++)
                sum += (unsigned char)line[i];
        first = false;
        if (line[n - 1] == '\n')
            line[--n] = '\0';
        else
            noeol = true;
        strs_add(&L, line);
    }
    free(line);
    bool rerr = ferror(f);
    fclose(f);
    if (rerr) {
        err("%s: %s", path, strerror(EIO));
        strs_free(&L);
        return false;
    }
    sf->sum_computed = sum & 0xffff;
    int i = 0;
    const char *why = NULL;
    const char *p;
#define BAD(w) do { why = (w); goto bad; } while (0)
    bool head_ok = L.n > 0 && (p = ctl(L.v[0], 'h')) && strlen(p) == 5 && !noeol;
    for (int k = 0; head_ok && k < 5; k++)
        if (!isdigit((unsigned char)p[k]))
            head_ok = false;
    if (!head_ok) {
        /* not an SCCS file at all, rather than a corrupted one */
        strs_free(&L);
        err("%s: not an SCCS file", path);
        return false;
    }
    sf->sum_stored = (unsigned)atoi(p);
    i = 1;
    /* the delta table */
    while (i < L.n && (p = ctl(L.v[i], 's'))) {
        struct delta d;
        memset(&d, 0, sizeof d);
        if (sscanf(p, " %ld/%ld/%ld", &d.ins, &d.del, &d.unch) != 3)
            BAD("bad statistics line");
        i++;
        if (i >= L.n || !(p = ctl(L.v[i], 'd')))
            BAD("missing delta line");
        char type, sidb[64], date[16], tim[16], user[256];
        int serial, pred;
        if (sscanf(p, " %c %63s %15s %15s %255s %d %d", &type, sidb, date, tim, user, &serial, &pred) != 7
            || (type != 'D' && type != 'R') || sid_parse(sidb, &d.sid) < 2 || strlen(date) != 8
            || strlen(tim) != 8 || serial <= 0 || pred < 0)
            BAD("bad delta line");
        if (d.sid.b && !d.sid.s)
            BAD("bad delta line");
        d.type = type;
        memcpy(d.date, date, 9);
        memcpy(d.time, tim, 9);
        d.user = xstrdup(user);
        d.serial = serial;
        d.pred = pred;
        i++;
        for (; i < L.n; i++) {
            const char *s = L.v[i];
            if (s[0] != SOH)
                BAD("bad delta table");
            if (s[1] == 'e' && s[2] == '\0')
                break;
            const char *rest = s + 2;
            if (*rest == ' ')
                rest++;
            bool ok = true;
            switch (s[1]) {
            case 'i': ok = int_list(rest, &d.inc, &d.ninc); break;
            case 'x': ok = int_list(rest, &d.exc, &d.nexc); break;
            case 'g': ok = int_list(rest, &d.ign, &d.nign); break;
            case 'm': strs_add(&d.mrs, rest); break;
            case 'c': strs_add(&d.comments, rest); break;
            default: ok = false;
            }
            if (!ok)
                BAD("bad delta table");
        }
        if (i >= L.n)
            BAD("missing end of delta");
        i++;
        sf->d = xrealloc(sf->d, (size_t)(sf->nd + 1) * sizeof *sf->d);
        sf->d[sf->nd++] = d;
        if (serial > sf->maxserial)
            sf->maxserial = serial;
    }
    /* users */
    if (i >= L.n || !(p = ctl(L.v[i], 'u')) || *p)
        BAD("missing user list");
    for (i++; i < L.n && !(L.v[i][0] == SOH && L.v[i][1] == 'U' && L.v[i][2] == '\0'); i++) {
        if (L.v[i][0] == SOH)
            BAD("bad user list");
        strs_add(&sf->users, L.v[i]);
    }
    if (i >= L.n)
        BAD("missing end of user list");
    i++;
    /* flags */
    for (; i < L.n && (p = ctl(L.v[i], 'f')); i++) {
        if (p[0] != ' ' || !islower((unsigned char)p[1]) || (p[2] && p[2] != ' '))
            BAD("bad flag line");
        int c = p[1] - 'a';
        free(flag_raw[c]);
        flag_raw[c] = xstrdup(p + 2);
        free(sf->flag[c]);
        sf->flag[c] = xstrdup(p[2] ? p + 3 : "");
    }
    /* descriptive text */
    if (i >= L.n || !(p = ctl(L.v[i], 't')) || *p)
        BAD("missing descriptive text");
    for (i++; i < L.n && !(L.v[i][0] == SOH && L.v[i][1] == 'T' && L.v[i][2] == '\0'); i++)
        strs_add(&sf->desc, L.v[i]);
    if (i >= L.n)
        BAD("missing end of descriptive text");
    i++;
    /* the body: control lines must be well formed and balanced */
    {
        int *open = xmalloc((size_t)(sf->maxserial + 2) * sizeof *open);
        memset(open, 0, (size_t)(sf->maxserial + 2) * sizeof *open);
        int nopen = 0;
        for (; i < L.n; i++) {
            const char *s = L.v[i];
            if (s[0] == SOH) {
                char *e;
                long v;
                if ((s[1] != 'I' && s[1] != 'D' && s[1] != 'E') || s[2] != ' '
                    || (v = strtol(s + 3, &e, 10)) <= 0 || *e || v > sf->maxserial) {
                    free(open);
                    BAD("bad control line in the body");
                }
                if (s[1] == 'E') {
                    if (!open[v]) {
                        free(open);
                        BAD("unbalanced control lines in the body");
                    }
                    open[v]--, nopen--;
                } else {
                    open[v]++, nopen++;
                }
            }
            strs_add(&sf->body, s);
        }
        free(open);
        if (nopen)
            BAD("unbalanced control lines in the body");
    }
    /* serials: unique, predecessors exist */
    for (int a = 0; a < sf->nd; a++) {
        for (int b = a + 1; b < sf->nd; b++)
            if (sf->d[a].serial == sf->d[b].serial)
                BAD("duplicate serial numbers");
        if (sf->d[a].pred && !by_serial(sf, sf->d[a].pred))
            BAD("a predecessor that does not exist");
    }
    strs_free(&L);
    if (!ignore_sum && sf->sum_stored != sf->sum_computed) {
        if (corrupt)
            *corrupt = true;
        err("%s: corrupted file: the checksum is wrong", path);
        return false;
    }
    return true;
bad:
    strs_free(&L);
    if (corrupt)
        *corrupt = true;
    err("%s: corrupted file: %s", path, why);
    return false;
#undef BAD
}

void sfile_free(struct sfile *sf)
{
    for (int i = 0; i < sf->nd; i++) {
        struct delta *d = &sf->d[i];
        free(d->user);
        free(d->inc);
        free(d->exc);
        free(d->ign);
        strs_free(&d->mrs);
        strs_free(&d->comments);
    }
    free(sf->d);
    strs_free(&sf->users);
    for (int i = 0; i < 26; i++)
        free(sf->flag[i]);
    strs_free(&sf->desc);
    strs_free(&sf->body);
    free(sf->path);
    memset(sf, 0, sizeof *sf);
}

/* ---- writing ---- */

struct obuf {
    char *s;
    size_t n, cap;
};

static void oput(struct obuf *o, const char *s, size_t n)
{
    if (o->n + n > o->cap) {
        while (o->n + n > o->cap)
            o->cap = o->cap ? 2 * o->cap : 4096;
        o->s = xrealloc(o->s, o->cap);
    }
    memcpy(o->s + o->n, s, n);
    o->n += n;
}

static void oprintf(struct obuf *o, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (k < 0)
        return;
    if ((size_t)k < sizeof tmp) {
        oput(o, tmp, (size_t)k);
        return;
    }
    char *big = xmalloc((size_t)k + 1);
    va_start(ap, fmt);
    vsnprintf(big, (size_t)k + 1, fmt, ap);
    va_end(ap);
    oput(o, big, (size_t)k);
    free(big);
}

static void olist(struct obuf *o, char c, const int *v, int n)
{
    if (!n)
        return;
    oprintf(o, "%c%c", SOH, c);
    for (int i = 0; i < n; i++)
        oprintf(o, " %d", v[i]);
    oput(o, "\n", 1);
}

/* the raw text of a flag line after "^Af x": as read, or made from the
   value (v keeps a blank even when empty, as CSSC writes it) */
static const char *raw_of(const struct sfile *sf, int c, char *buf, size_t size)
{
    const char *v = sf->flag[c];
    if (flag_raw[c] && strcmp(flag_raw[c][0] ? flag_raw[c] + 1 : "", v) == 0)
        return flag_raw[c];
    if (*v || c == 'v' - 'a')
        snprintf(buf, size, " %s", v);
    else
        buf[0] = '\0';
    return buf;
}

bool sfile_write(struct sfile *sf, mode_t mode)
{
    struct obuf o = { 0 };
    char sidb[64];
    for (int i = 0; i < sf->nd; i++) {
        const struct delta *d = &sf->d[i];
        oprintf(&o, "%cs %05ld/%05ld/%05ld\n", SOH, d->ins > 99999 ? 99999 : d->ins,
                d->del > 99999 ? 99999 : d->del, d->unch > 99999 ? 99999 : d->unch);
        oprintf(&o, "%cd %c %s %s %s %s %d %d\n", SOH, d->type, sid_str(&d->sid, sidb),
                d->date, d->time, d->user, d->serial, d->pred);
        olist(&o, 'i', d->inc, d->ninc);
        olist(&o, 'x', d->exc, d->nexc);
        olist(&o, 'g', d->ign, d->nign);
        for (int k = 0; k < d->mrs.n; k++)
            oprintf(&o, "%cm %s\n", SOH, d->mrs.v[k]);
        for (int k = 0; k < d->comments.n; k++)
            oprintf(&o, "%cc %s\n", SOH, d->comments.v[k]);
        oprintf(&o, "%ce\n", SOH);
    }
    oprintf(&o, "%cu\n", SOH);
    for (int k = 0; k < sf->users.n; k++)
        oprintf(&o, "%s\n", sf->users.v[k]);
    oprintf(&o, "%cU\n", SOH);
    /* the flags in order, e last, as CSSC writes them */
    for (int pass = 0; pass < 2; pass++)
        for (int c = 0; c < 26; c++) {
            if (!sf->flag[c] || (pass == 0) == (c == 'e' - 'a'))
                continue;
            char buf[4096];
            oprintf(&o, "%cf %c%s\n", SOH, 'a' + c, raw_of(sf, c, buf, sizeof buf));
        }
    oprintf(&o, "%ct\n", SOH);
    for (int k = 0; k < sf->desc.n; k++)
        oprintf(&o, "%s\n", sf->desc.v[k]);
    oprintf(&o, "%cT\n", SOH);
    for (int k = 0; k < sf->body.n; k++) {
        oput(&o, sf->body.v[k], strlen(sf->body.v[k]));
        oput(&o, "\n", 1);
    }
    unsigned sum = 0;
    for (size_t k = 0; k < o.n; k++)
        sum += (unsigned char)o.s[k];
    char head[16];
    snprintf(head, sizeof head, "%ch%05u\n", SOH, sum & 0xffff);

    char *x = aux_name(sf->path, 'x');
    int fd = open(x, O_WRONLY | O_CREAT | O_EXCL, mode & 0777);
    if (fd < 0 && errno == EEXIST && unlink(x) == 0)
        fd = open(x, O_WRONLY | O_CREAT | O_EXCL, mode & 0777);
    if (fd < 0) {
        err("%s: %s", x, strerror(errno));
        free(x);
        free(o.s);
        return false;
    }
    bool ok = write(fd, head, strlen(head)) == (ssize_t)strlen(head);
    size_t done = 0;
    while (ok && done < o.n) {
        ssize_t w = write(fd, o.s + done, o.n - done);
        if (w <= 0) {
            ok = false;
            break;
        }
        done += (size_t)w;
    }
    if (close(fd) != 0)
        ok = false;
    free(o.s);
    if (!ok || chmod(x, mode & 07777) != 0 || rename(x, sf->path) != 0) {
        err("%s: %s", sf->path, strerror(errno ? errno : EIO));
        unlink(x);
        free(x);
        return false;
    }
    free(x);
    return true;
}

/* ---- looking things up ---- */

struct delta *by_serial(const struct sfile *sf, int serial)
{
    for (int i = 0; i < sf->nd; i++)
        if (sf->d[i].serial == serial)
            return &sf->d[i];
    return NULL;
}

struct delta *by_sid(const struct sfile *sf, const struct sid *s)
{
    for (int i = 0; i < sf->nd; i++)
        if (sf->d[i].type == 'D' && sid_eq(&sf->d[i].sid, s))
            return &sf->d[i];
    return NULL;
}

const char *flagval(const struct sfile *sf, char c)
{
    return c >= 'a' && c <= 'z' ? sf->flag[c - 'a'] : NULL;
}

const char *base_name(const char *path)
{
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

const char *module_name(const struct sfile *sf)
{
    const char *m = flagval(sf, 'm');
    if (m && *m)
        return m;
    const char *b = base_name(sf->path);
    return strncmp(b, "s.", 2) == 0 ? b + 2 : b;
}

char *aux_name(const char *path, char tag)
{
    const char *b = base_name(path);
    const char *rest = strncmp(b, "s.", 2) == 0 ? b + 2 : b;
    if (tag == 'g')
        return xstrdup(rest);
    size_t dl = (size_t)(b - path);
    char *r = xmalloc(dl + strlen(rest) + 3);
    memcpy(r, path, dl);
    r[dl] = tag;
    r[dl + 1] = '.';
    strcpy(r + dl + 2, rest);
    return r;
}

/* ---- choosing the delta to get ---- */

static bool is_d(const struct delta *d)
{
    return d->type == 'D';
}

/* the highest trunk level in release r, or NULL */
static struct delta *top_trunk(const struct sfile *sf, int r)
{
    struct delta *best = NULL;
    for (int i = 0; i < sf->nd; i++) {
        struct delta *d = &sf->d[i];
        if (is_d(d) && !d->sid.b && d->sid.r == r && (!best || d->sid.l > best->sid.l))
            best = d;
    }
    return best;
}

static int max_release(const struct sfile *sf)
{
    int m = 0;
    for (int i = 0; i < sf->nd; i++)
        if (is_d(&sf->d[i]) && !sf->d[i].sid.b && sf->d[i].sid.r > m)
            m = sf->d[i].sid.r;
    return m;
}

bool choose(const struct sfile *sf, const char *rsid, bool use_t, struct delta **got)
{
    char buf[64];
    *got = NULL;
    if (!rsid || !*rsid) {
        const char *d = flagval(sf, 'd');
        if (d && *d)
            rsid = d;
    }
    int mR = max_release(sf);
    if (!mR) {
        err("%s: no deltas", sf->path);
        return false;
    }
    struct sid s;
    int n;
    char top[16];
    if (!rsid || !*rsid) {
        if (!use_t) {
            *got = top_trunk(sf, mR);
            return true;
        }
        snprintf(top, sizeof top, "%d", mR);    /* -t alone: the top of release mR */
        rsid = top;
    }
    n = sid_parse(rsid, &s);
    if (!n) {
        err("%s: invalid SID: %s", sf->path, rsid);
        return false;
    }
    struct delta *best = NULL;
    if (use_t && n <= 2) {
        /* the most recent delta in the release (and level) */
        for (int i = 0; i < sf->nd; i++) {
            struct delta *d = &sf->d[i];
            if (is_d(d) && d->sid.r == s.r && (n == 1 || d->sid.l == s.l)
                && (!best || d->serial > best->serial))
                best = d;
        }
    } else if (n == 1) {
        best = top_trunk(sf, s.r);
        if (!best && mR && s.r > mR)
            best = top_trunk(sf, mR);
        else if (!best)
            for (int r = s.r - 1; r > 0 && !best; r--)
                best = top_trunk(sf, r);
    } else if (n == 2) {
        best = by_sid(sf, &s);
    } else if (n == 3) {
        for (int i = 0; i < sf->nd; i++) {
            struct delta *d = &sf->d[i];
            if (is_d(d) && d->sid.r == s.r && d->sid.l == s.l && d->sid.b == s.b
                && (!best || d->sid.s > best->sid.s))
                best = d;
        }
    } else {
        best = by_sid(sf, &s);
    }
    if (!best) {
        err("%s: nonexistent SID: %s", sf->path, sid_str(&s, buf));
        return false;
    }
    *got = best;
    return true;
}

static bool reserved_sid(const struct strs *reserved, const struct sid *s)
{
    char buf[64];
    sid_str(s, buf);
    for (int i = 0; reserved && i < reserved->n; i++)
        if (strcmp(reserved->v[i], buf) == 0)
            return true;
    return false;
}

static bool sid_taken(const struct sfile *sf, const struct strs *reserved, const struct sid *s)
{
    for (int i = 0; i < sf->nd; i++)
        if (sf->d[i].type == 'D' && sid_eq(&sf->d[i].sid, s))
            return true;
    return reserved_sid(reserved, s);
}

/* a new branch R.L.(mB+1).1 from got */
static struct sid new_branch(const struct sfile *sf, const struct delta *got, const struct strs *reserved)
{
    int mb = 0;
    for (int i = 0; i < sf->nd; i++) {
        const struct delta *d = &sf->d[i];
        if (d->type == 'D' && d->sid.r == got->sid.r && d->sid.l == got->sid.l && d->sid.b > mb)
            mb = d->sid.b;
    }
    for (int i = 0; reserved && i < reserved->n; i++) {
        struct sid t;
        if (sid_parse(reserved->v[i], &t) == 4 && t.r == got->sid.r && t.l == got->sid.l && t.b > mb)
            mb = t.b;
    }
    struct sid s = { got->sid.r, got->sid.l, mb + 1, 1 };
    return s;
}

bool new_sid(const struct sfile *sf, const char *rsid, const struct delta *got,
             bool want_branch, const struct strs *reserved, struct sid *out)
{
    if (!rsid || !*rsid) {
        const char *d = flagval(sf, 'd');
        if (d && *d)
            rsid = d;
    }
    struct sid s = { 0, 0, 0, 0 };
    int n = rsid && *rsid ? sid_parse(rsid, &s) : 0;
    bool branch = want_branch;
    struct sid r = got->sid;
    if (!got->sid.b) {
        /* a trunk delta: a successor on the trunk makes a branch */
        for (int i = 0; i < sf->nd; i++)
            if (is_d(&sf->d[i]) && sf->d[i].pred == got->serial && !sf->d[i].sid.b)
                branch = true;
        if (n == 1 && s.r < got->sid.r)
            branch = true;      /* hR.mL: the release asked for does not exist */
        if (n == 1 && s.r < max_release(sf) && s.r == got->sid.r && top_trunk(sf, s.r) == got) {
            /* R exists and has a trunk successor in a later release */
            for (int i = 0; i < sf->nd; i++)
                if (is_d(&sf->d[i]) && !sf->d[i].sid.b && sf->d[i].sid.r > s.r)
                    branch = true;
        }
        if (!branch) {
            if (n == 1 && s.r > got->sid.r)
                r = (struct sid){ s.r, 1, 0, 0 };
            else
                r = (struct sid){ got->sid.r, got->sid.l + 1, 0, 0 };
        }
    } else {
        for (int i = 0; i < sf->nd; i++) {
            const struct delta *d = &sf->d[i];
            if (is_d(d) && d->pred == got->serial && d->sid.r == got->sid.r && d->sid.l == got->sid.l
                && d->sid.b == got->sid.b)
                branch = true;
        }
        if (!branch)
            r = (struct sid){ got->sid.r, got->sid.l, got->sid.b, got->sid.s + 1 };
    }
    if (branch || sid_taken(sf, reserved, &r))
        r = new_branch(sf, got, reserved);
    *out = r;
    return true;
}

/* ---- the deltas applied ---- */

static bool is_ancestor(const struct sfile *sf, const struct delta *a, const struct delta *b)
{
    for (const struct delta *d = b; d; d = d->pred ? by_serial(sf, d->pred) : NULL)
        if (d == a)
            return true;
    return false;
}

bool sid_list(const struct sfile *sf, const char *list, int **serials, int *n)
{
    *serials = NULL;
    *n = 0;
    char *copy = xstrdup(list), *save = NULL;
    bool ok = true;
    for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char *dash = strchr(tok, '-');
        struct sid a, b;
        if (dash)
            *dash = '\0';
        if (!sid_parse(tok, &a) || (dash && !sid_parse(dash + 1, &b))) {
            err("%s: invalid SID in a list: %s", sf->path, tok);
            ok = false;
            break;
        }
        struct delta *da = by_sid(sf, &a), *db = dash ? by_sid(sf, &b) : da;
        if (!da || !db) {
            char buf[64];
            err("%s: nonexistent SID in a list: %s", sf->path, sid_str(da ? &b : &a, buf));
            ok = false;
            break;
        }
        if (!is_ancestor(sf, da, db)) {
            err("%s: %s is not an ancestor of %s", sf->path, tok, dash + 1);
            ok = false;
            break;
        }
        for (const struct delta *d = db; d; d = d->pred ? by_serial(sf, d->pred) : NULL) {
            *serials = xrealloc(*serials, (size_t)(*n + 1) * sizeof **serials);
            (*serials)[(*n)++] = d->serial;
            if (d == da)
                break;
        }
    }
    free(copy);
    return ok;
}

bool applied_set(const struct sfile *sf, const struct delta *target, const char *ilist,
                 const char *xlist, time_t cutoff, int **state)
{
    int max = sf->maxserial;
    int *st = xmalloc((size_t)(max + 1) * sizeof *st);
    bool *user = xmalloc((size_t)(max + 1));
    memset(st, 0, (size_t)(max + 1) * sizeof *st);
    memset(user, 0, (size_t)(max + 1));
    for (const struct delta *d = target; d; d = d->pred ? by_serial(sf, d->pred) : NULL)
        st[d->serial] |= AP_PATH | AP_APPLIED;
    int *v, n;
    /* -x, then -i: a delta in both is included, as in CSSC */
    if (xlist) {
        if (!sid_list(sf, xlist, &v, &n)) {
            free(st), free(user);
            return false;
        }
        for (int i = 0; i < n; i++) {
            st[v[i]] = (st[v[i]] | AP_EXCLUDED) & ~(AP_APPLIED | AP_INCLUDED);
            user[v[i]] = true;
        }
        free(v);
    }
    if (ilist) {
        if (!sid_list(sf, ilist, &v, &n)) {
            free(st), free(user);
            return false;
        }
        for (int i = 0; i < n; i++) {
            st[v[i]] = (st[v[i]] | AP_INCLUDED | AP_APPLIED) & ~AP_EXCLUDED;
            user[v[i]] = true;
        }
        free(v);
    }
    /* the lists of the deltas applied, from the newest */
    for (int s = max; s > 0; s--) {
        const struct delta *d = by_serial(sf, s);
        if (!d || !(st[s] & AP_APPLIED))
            continue;
        for (int k = 0; k < d->ninc; k++)
            if (d->inc[k] <= max && !user[d->inc[k]])
                st[d->inc[k]] |= AP_INCLUDED | AP_APPLIED;
        for (int k = 0; k < d->nexc; k++)
            if (d->exc[k] <= max && !user[d->exc[k]])
                st[d->exc[k]] = (st[d->exc[k]] | AP_EXCLUDED) & ~AP_APPLIED;
        for (int k = 0; k < d->nign; k++)
            if (d->ign[k] <= max && !user[d->ign[k]])
                st[d->ign[k]] = (st[d->ign[k]] | AP_IGNORED) & ~AP_APPLIED;
    }
    for (int i = 0; i < sf->nd; i++) {
        const struct delta *d = &sf->d[i];
        if (d->type != 'D')
            st[d->serial] &= ~AP_APPLIED;
        if (cutoff && delta_time(d) > cutoff && (st[d->serial] & (AP_APPLIED | AP_PATH)))
            st[d->serial] = (st[d->serial] | AP_CUTOFF) & ~AP_APPLIED;
    }
    free(user);
    *state = st;
    return true;
}

void retrieve(const struct sfile *sf, const int *state, struct text *t)
{
    memset(t, 0, sizeof *t);
    int *istack = xmalloc((size_t)(sf->maxserial + 2) * sizeof *istack);
    int *dcount = xmalloc((size_t)(sf->maxserial + 2) * sizeof *dcount);
    memset(dcount, 0, (size_t)(sf->maxserial + 2) * sizeof *dcount);
    int ni = 0, dapplied = 0, cap = 0;
    for (int k = 0; k < sf->body.n; k++) {
        const char *s = sf->body.v[k];
        if (s[0] == SOH) {
            int v = atoi(s + 3);
            if (s[1] == 'I') {
                istack[ni++] = v;
            } else if (s[1] == 'D') {
                if (dcount[v]++ == 0 && (state[v] & AP_APPLIED))
                    dapplied++;
            } else {
                /* E: the innermost open block of v */
                int j;
                for (j = ni - 1; j >= 0 && istack[j] != v; j--)
                    ;
                if (j >= 0) {
                    memmove(istack + j, istack + j + 1, (size_t)(ni - j - 1) * sizeof *istack);
                    ni--;
                } else if (dcount[v] > 0) {
                    if (--dcount[v] == 0 && (state[v] & AP_APPLIED))
                        dapplied--;
                }
            }
            continue;
        }
        if (ni == 0 || !(state[istack[ni - 1]] & AP_APPLIED) || dapplied > 0)
            continue;
        if (t->lines.n == cap) {
            cap = cap ? 2 * cap : 256;
            t->by = xrealloc(t->by, (size_t)cap * sizeof *t->by);
        }
        t->by[t->lines.n] = istack[ni - 1];
        strs_add(&t->lines, s);
    }
    free(istack);
    free(dcount);
}

void text_free(struct text *t)
{
    strs_free(&t->lines);
    free(t->by);
    t->by = NULL;
}

/* ---- dates ---- */

void now_strings(char *date, char *tim)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char b[64];
    snprintf(b, sizeof b, "%02u/%02u/%02u", (unsigned)tm->tm_year % 100, (unsigned)tm->tm_mon % 12 + 1,
             (unsigned)tm->tm_mday % 32);
    memcpy(date, b, 9);
    snprintf(b, sizeof b, "%02u:%02u:%02u", (unsigned)tm->tm_hour % 24, (unsigned)tm->tm_min % 60,
             (unsigned)tm->tm_sec % 61);
    memcpy(tim, b, 9);
}

time_t delta_time(const struct delta *d)
{
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    int y, mo, da, h, mi, s;
    if (sscanf(d->date, "%d/%d/%d", &y, &mo, &da) != 3 || sscanf(d->time, "%d:%d:%d", &h, &mi, &s) != 3)
        return 0;
    if (y < 100)
        y += y < 69 ? 2000 : 1900;
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = da;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = s;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

bool parse_cutoff(const char *s, time_t *t)
{
    int v[6], n = 0;
    while (*s && n < 6) {
        if (!isdigit((unsigned char)*s)) {
            s++;
            continue;
        }
        int x = *s++ - '0';
        if (isdigit((unsigned char)*s))
            x = x * 10 + (*s++ - '0');
        v[n++] = x;
    }
    while (*s && !isdigit((unsigned char)*s))
        s++;
    if (n == 0 || *s)
        return false;
    int y = v[0] < 69 ? 2000 + v[0] : 1900 + v[0];
    int mo = n > 1 ? v[1] : 12;
    if (mo < 1 || mo > 12)
        return false;
    static const int mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int last = mdays[mo - 1] + (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0));
    int da = n > 2 ? v[2] : last;
    int h = n > 3 ? v[3] : 23, mi = n > 4 ? v[4] : 59, se = n > 5 ? v[5] : 59;
    if (da < 1 || da > last || h > 23 || mi > 59 || se > 60)
        return false;
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = y - 1900, tm.tm_mon = mo - 1, tm.tm_mday = da;
    tm.tm_hour = h, tm.tm_min = mi, tm.tm_sec = se, tm.tm_isdst = -1;
    *t = mktime(&tm);
    return true;
}

/* ---- users ---- */

const char *login_name(void)
{
    static char name[64];
    if (!name[0]) {
        struct passwd *pw = getpwuid(getuid());
        if (pw && pw->pw_name)
            snprintf(name, sizeof name, "%s", pw->pw_name);
        else
            snprintf(name, sizeof name, "%ld", (long)getuid());
    }
    return name;
}

static bool in_group(const char *login, gid_t g)
{
    struct passwd *pw = getpwnam(login);
    if (pw && pw->pw_gid == g)
        return true;
    struct group *gr = getgrgid(g);
    if (gr)
        for (char **m = gr->gr_mem; *m; m++)
            if (strcmp(*m, login) == 0)
                return true;
    return false;
}

bool may_edit(const struct sfile *sf, const char *login)
{
    if (sf->users.n == 0)
        return true;
    bool allowed = false, any_positive = false;
    for (int i = 0; i < sf->users.n; i++) {
        const char *u = sf->users.v[i];
        bool neg = u[0] == '!';
        if (neg)
            u++;
        else
            any_positive = true;
        bool num = *u != '\0';
        for (const char *p = u; *p; p++)
            if (!isdigit((unsigned char)*p))
                num = false;
        bool match = num ? in_group(login, (gid_t)atol(u)) : strcmp(u, login) == 0;
        if (match && neg)
            return false;
        if (match)
            allowed = true;
    }
    (void)any_positive;
    /* a list that is not empty names the users who may (as CSSC has it,
       also when it only names users who may not) */
    return allowed;
}

/* ---- the p-file ---- */

static char *field(char **p)
{
    while (**p == ' ')
        (*p)++;
    if (!**p)
        return NULL;
    char *s = *p;
    while (**p && **p != ' ')
        (*p)++;
    if (**p)
        *(*p)++ = '\0';
    return xstrdup(s);
}

bool pfile_read(const char *path, struct pfile *p)
{
    memset(p, 0, sizeof *p);
    FILE *f = fopen(path, "r");
    if (!f)
        return errno == ENOENT;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    bool ok = true;
    while ((n = getline(&line, &cap, f)) > 0) {
        if (line[n - 1] == '\n')
            line[--n] = '\0';
        struct pentry e;
        memset(&e, 0, sizeof e);
        char *q = line;
        e.got = field(&q), e.newsid = field(&q), e.user = field(&q);
        e.date = field(&q), e.time = field(&q);
        char *x;
        while ((x = field(&q))) {
            if (strncmp(x, "-i", 2) == 0)
                e.ilist = xstrdup(x + 2);
            else if (strncmp(x, "-x", 2) == 0)
                e.xlist = xstrdup(x + 2);
            free(x);
        }
        if (!e.time) {
            ok = false;
            free(e.got), free(e.newsid), free(e.user), free(e.date);
            continue;
        }
        p->e = xrealloc(p->e, (size_t)(p->n + 1) * sizeof *p->e);
        p->e[p->n++] = e;
    }
    free(line);
    fclose(f);
    if (!ok)
        err("%s: bad p-file", path);
    return ok;
}

bool pfile_write(const char *path, const struct pfile *p)
{
    if (p->n == 0) {
        if (unlink(path) != 0 && errno != ENOENT) {
            err("%s: %s", path, strerror(errno));
            return false;
        }
        return true;
    }
    char *q = aux_name(path, 'q');
    /* path is the p-file itself: make the q-file next to it */
    {
        const char *b = base_name(path);
        size_t dl = (size_t)(b - path);
        free(q);
        q = xmalloc(strlen(path) + 1);
        memcpy(q, path, dl);
        strcpy(q + dl, b);
        q[dl] = 'q';
    }
    FILE *f = fopen(q, "w");
    if (!f) {
        err("%s: %s", q, strerror(errno));
        free(q);
        return false;
    }
    for (int i = 0; i < p->n; i++) {
        const struct pentry *e = &p->e[i];
        fprintf(f, "%s %s %s %s %s", e->got, e->newsid, e->user, e->date, e->time);
        if (e->ilist)
            fprintf(f, " -i%s", e->ilist);
        if (e->xlist)
            fprintf(f, " -x%s", e->xlist);
        fputc('\n', f);
    }
    bool ok = fclose(f) == 0;
    if (!ok || chmod(q, 0644) != 0 || rename(q, path) != 0) {
        err("%s: %s", path, strerror(errno));
        unlink(q);
        free(q);
        return false;
    }
    free(q);
    return true;
}

void pfile_free(struct pfile *p)
{
    for (int i = 0; i < p->n; i++) {
        struct pentry *e = &p->e[i];
        free(e->got), free(e->newsid), free(e->user), free(e->date), free(e->time);
        free(e->ilist), free(e->xlist);
    }
    free(p->e);
    memset(p, 0, sizeof *p);
}

/* ---- the lock ---- */

static char *zname;
static volatile sig_atomic_t locked;

static void on_signal(int sig)
{
    if (locked && zname)
        unlink(zname);
    signal(sig, SIG_DFL);
    raise(sig);
}

bool lock_sccs(const char *path)
{
    free(zname);
    zname = aux_name(path, 'z');
    int fd = open(zname, O_WRONLY | O_CREAT | O_EXCL, 0444);
    if (fd < 0) {
        if (errno == EEXIST)
            err("%s: the SCCS file is locked (%s exists)", path, zname);
        else
            err("%s: %s", zname, strerror(errno));
        return false;
    }
    pid_t pid = getpid();
    if (write(fd, &pid, sizeof pid) != (ssize_t)sizeof pid) {
        close(fd);
        unlink(zname);
        err("%s: %s", zname, strerror(errno));
        return false;
    }
    close(fd);
    locked = 1;
    static bool installed;
    if (!installed) {
        int sigs[] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM };
        for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
            struct sigaction old;
            if (sigaction(sigs[i], NULL, &old) == 0 && old.sa_handler != SIG_IGN)
                signal(sigs[i], on_signal);
        }
        installed = true;
    }
    return true;
}

void unlock_sccs(void)
{
    if (locked && zname)
        unlink(zname);
    locked = 0;
}

/* ---- operands ---- */

static bool sccs_name(const char *path)
{
    return strncmp(base_name(path), "s.", 2) == 0;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

void for_each_file(int argc, char **argv, void (*fn)(const char *), bool *multi)
{
    *multi = argc > 1;
    if (argc == 1 && strcmp(argv[0], "-") == 0) {
        *multi = true;
        char *line = NULL;
        size_t cap = 0;
        ssize_t n;
        while ((n = getline(&line, &cap, stdin)) > 0) {
            if (line[n - 1] == '\n')
                line[--n] = '\0';
            if (n == 0 || !sccs_name(line) || access(line, R_OK) != 0)
                continue;
            fn(line);
        }
        free(line);
        return;
    }
    struct stat st;
    for (int i = 0; i < argc; i++)
        if (stat(argv[i], &st) == 0 && S_ISDIR(st.st_mode))
            *multi = true;
    for (int i = 0; i < argc; i++) {
        if (stat(argv[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            DIR *d = opendir(argv[i]);
            if (!d) {
                err("%s: %s", argv[i], strerror(errno));
                continue;
            }
            struct strs names = { 0 };
            struct dirent *de;
            while ((de = readdir(d)))
                if (strncmp(de->d_name, "s.", 2) == 0 && de->d_name[2])
                    strs_add(&names, de->d_name);
            closedir(d);
            if (names.n)
                qsort(names.v, (size_t)names.n, sizeof *names.v, cmp_str);
            for (int k = 0; k < names.n; k++) {
                size_t l = strlen(argv[i]);
                char *p = xasprintf("%s%s%s", argv[i], l && argv[i][l - 1] == '/' ? "" : "/", names.v[k]);
                struct stat fs;
                if (stat(p, &fs) == 0 && S_ISREG(fs.st_mode) && access(p, R_OK) == 0)
                    fn(p);
                free(p);
            }
            strs_free(&names);
            continue;
        }
        fn(argv[i]);
    }
}

/* ---- comments and MRs ---- */

bool read_continued(const char *prompt, struct strs *out)
{
    if (isatty(0)) {
        fputs(prompt, stdout);
        fflush(stdout);
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    bool any = false;
    while ((n = getline(&line, &cap, stdin)) > 0) {
        any = true;
        if (line[n - 1] == '\n')
            line[--n] = '\0';
        if (n > 0 && line[n - 1] == '\\') {
            line[--n] = '\0';
            strs_add(out, line);
            continue;
        }
        strs_add(out, line);
        break;
    }
    free(line);
    return any;
}

void split_mrs(const char *s, struct strs *out)
{
    while (*s) {
        while (*s && isspace((unsigned char)*s))
            s++;
        const char *b = s;
        while (*s && !isspace((unsigned char)*s))
            s++;
        if (s > b) {
            char *w = xstrndup(b, (size_t)(s - b));
            strs_add(out, w);
            free(w);
        }
    }
}

bool check_mrs(const struct sfile *sf, const struct strs *mrs)
{
    const char *pgm = flagval(sf, 'v');
    if (!pgm || !*pgm)
        return true;
    char **av = xmalloc((size_t)(mrs->n + 4) * sizeof *av);
    int k = 0;
    av[k++] = (char *)pgm;
    av[k++] = (char *)module_name(sf);
    const char *t = flagval(sf, 't');
    av[k++] = (char *)(t ? t : "");
    for (int i = 0; i < mrs->n; i++)
        av[k++] = mrs->v[i];
    av[k] = NULL;
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        execvp(pgm, av);
        _exit(127);
    }
    free(av);
    int st;
    if (pid < 0 || waitpid(pid, &st, 0) < 0)
        return false;
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

/* ---- id keywords ---- */

/* the date of the newest delta applied: yy/mm/dd, hh:mm:ss */
static const struct delta *newest_applied(const struct sfile *sf, const int *st)
{
    const struct delta *best = NULL;
    for (int i = 0; i < sf->nd; i++)
        if ((st[sf->d[i].serial] & AP_APPLIED) && (!best || delta_time(&sf->d[i]) > delta_time(best)))
            best = &sf->d[i];
    return best;
}


static void put_kw(char **buf, size_t *len, size_t *cap, const char *s)
{
    size_t n = strlen(s);
    if (*len + n + 1 > *cap) {
        while (*len + n + 1 > *cap)
            *cap = *cap ? 2 * *cap : 256;
        *buf = xrealloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = '\0';
}

/* line with its keywords replaced; *found set if there was one */
char *kw_expand(const struct kwctx *k, const char *line, long lineno, bool *found)
{
    char *buf = NULL;
    size_t len = 0, cap = 0;
    char tmp[8200 + 64], sidb[64];
    const struct sid *g = &k->got->sid;
    put_kw(&buf, &len, &cap, "");
    for (const char *p = line; *p; ) {
        if (p[0] == '%' && p[1] && p[2] == '%' && strchr("MIRLBSDHTEGUYFPQCZWA", p[1])) {
            const char *y = flagval(k->sf, 't'), *q = flagval(k->sf, 'q');
            const char *ed = k->newest ? k->newest->date : "", *et = k->newest ? k->newest->time : "";
            *found = true;
            switch (p[1]) {
            case 'M': snprintf(tmp, sizeof tmp, "%s", module_name(k->sf)); break;
            case 'I': snprintf(tmp, sizeof tmp, "%s", sid_str(g, sidb)); break;
            case 'R': snprintf(tmp, sizeof tmp, "%d", g->r); break;
            case 'L': snprintf(tmp, sizeof tmp, "%d", g->l); break;
            case 'B': snprintf(tmp, sizeof tmp, "%d", g->b); break;
            case 'S': snprintf(tmp, sizeof tmp, "%d", g->s); break;
            case 'D': snprintf(tmp, sizeof tmp, "%s", k->today); break;
            case 'H': snprintf(tmp, sizeof tmp, "%.2s/%.2s/%.2s", k->today + 3, k->today + 6, k->today); break;
            case 'T': snprintf(tmp, sizeof tmp, "%s", k->now); break;
            case 'E': snprintf(tmp, sizeof tmp, "%s", ed); break;
            case 'G': snprintf(tmp, sizeof tmp, "%.2s/%.2s/%.2s", ed + 3, ed + 6, ed); break;
            case 'U': snprintf(tmp, sizeof tmp, "%s", et); break;
            case 'Y': snprintf(tmp, sizeof tmp, "%s", y ? y : ""); break;
            case 'F': snprintf(tmp, sizeof tmp, "%s", base_name(k->sf->path)); break;
            case 'P': snprintf(tmp, sizeof tmp, "%s", k->abspath); break;
            case 'Q': snprintf(tmp, sizeof tmp, "%s", q ? q : ""); break;
            case 'C': snprintf(tmp, sizeof tmp, "%ld", lineno); break;
            case 'Z': snprintf(tmp, sizeof tmp, "@(#)"); break;
            case 'W': snprintf(tmp, sizeof tmp, "@(#)%s\t%s", module_name(k->sf), sid_str(g, sidb)); break;
            case 'A': snprintf(tmp, sizeof tmp, "@(#)%s %s %s@(#)", y ? y : "", module_name(k->sf), sid_str(g, sidb)); break;
            }
            put_kw(&buf, &len, &cap, tmp);
            p += 3;
            continue;
        }
        char c[2] = { *p++, '\0' };
        put_kw(&buf, &len, &cap, c);
    }
    return buf;
}

void kw_init(struct kwctx *k, const struct sfile *sf, const struct delta *got, const int *state)
{
    memset(k, 0, sizeof *k);
    k->sf = sf;
    /* an excluded delta: the nearest one before it that is applied, as
       CSSC has it */
    while (got && (state[got->serial] & AP_EXCLUDED) && got->pred)
        got = by_serial(sf, got->pred);
    k->got = got;
    k->newest = newest_applied(sf, state);
    now_strings(k->today, k->now);
    if (sf->path[0] == '/') {
        snprintf(k->abspath, sizeof k->abspath, "%s", sf->path);
    } else {
        char cwd[4096];
        if (getcwd(cwd, sizeof cwd))
            snprintf(k->abspath, sizeof k->abspath, "%s/%s", cwd, sf->path);
        else
            snprintf(k->abspath, sizeof k->abspath, "%s", sf->path);
    }
}



bool has_keywords(const char *line)
{
    for (const char *p = strchr(line, '%'); p; p = strchr(p + 1, '%'))
        if (p[1] && strchr("MIRLBSDHTEGUYFPQCZWA", p[1]) && p[2] == '%')
            return true;
    return false;
}

/* ---- options ---- */

int sopt(int argc, char **argv, const char *spec, struct opts *o)
{
    o->arg = NULL;
    if (o->pos == 0) {
        if (o->ind >= argc || argv[o->ind][0] != '-' || argv[o->ind][1] == '\0')
            return -1;
        if (strcmp(argv[o->ind], "--") == 0) {
            o->ind++;
            return -1;
        }
        o->pos = 1;
    }
    char *a = argv[o->ind];
    char c = a[o->pos++];
    const char *p = c != ':' && c != ';' ? strchr(spec, c) : NULL;
    if (!p) {
        warn("unknown option -%c", c);
        if (!a[o->pos])
            o->ind++, o->pos = 0;
        return '?';
    }
    if (p[1] == ':' || p[1] == ';') {
        if (a[o->pos]) {
            o->arg = a + o->pos;
        } else if (p[1] == ';') {
            o->arg = a + o->pos;        /* "" */
        } else if (o->ind + 1 < argc) {
            o->arg = argv[++o->ind];
        } else {
            warn("option -%c needs an argument", c);
            o->ind++, o->pos = 0;
            return '?';
        }
        o->ind++, o->pos = 0;
        return c;
    }
    if (!a[o->pos])
        o->ind++, o->pos = 0;
    return c;
}
