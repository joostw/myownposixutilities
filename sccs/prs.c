/*
 * prs.c - prs: print an SCCS file (POSIX.1-2024, XCU prs)
 *
 *   prs [-a] [-d dataspec] [-r[SID]] file...
 *   prs [-e|-l] -c cutoff [-d dataspec] file...
 *   prs [-e|-l] -r[SID] [-d dataspec] file...
 */

#include "sccs.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static bool o_a, o_e, o_l, o_r;
static const char *o_d, *o_rsid, *o_c;
static time_t cutoff;

struct out {
    char *s;
    size_t n, cap;
};

static void put(struct out *o, const char *s, size_t n)
{
    if (o->n + n + 1 > o->cap) {
        while (o->n + n + 1 > o->cap)
            o->cap = o->cap ? 2 * o->cap : 256;
        o->s = xrealloc(o->s, o->cap);
    }
    memcpy(o->s + o->n, s, n);
    o->n += n;
    o->s[o->n] = '\0';
}

static void puts_(struct out *o, const char *s)
{
    put(o, s, strlen(s));
}

static void putf(struct out *o, const char *fmt, ...)
{
    char tmp[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    puts_(o, tmp);
}

static void serials(struct out *o, const int *v, int n)
{
    for (int i = 0; i < n; i++)
        putf(o, "%s%d", i ? " " : "", v[i]);
}

static const char *yesno(bool b)
{
    return b ? "yes" : "no";
}

static const char *or_none(const char *s)
{
    return s ? s : "none";
}

/* the flags as prs writes them (:FL:) */
static void flag_list(struct out *o, const struct sfile *sf)
{
    static const struct { char c; const char *name; bool value; } fl[] = {
        { 'b', "branch", false }, { 'c', "ceiling", true }, { 'd', "default SID", true },
        { 'f', "floor", true }, { 'i', "id keywd err/warn", false }, { 'j', "joint edit", false },
        { 'l', "locked releases", true }, { 'm', "module", true }, { 'n', "null delta", false },
        { 'q', "csect name", true }, { 't', "type", true }, { 'v', "validate MRs", true },
    };
    for (size_t i = 0; i < sizeof fl / sizeof fl[0]; i++) {
        const char *v = flagval(sf, fl[i].c);
        if (!v)
            continue;
        if (fl[i].value)
            putf(o, "%s\t%s\n", fl[i].name, v);
        else
            putf(o, "%s\n", fl[i].name);
    }
}

static void expand_spec(struct out *o, const char *spec, const struct sfile *sf, const struct delta *d)
{
    char sidb[64];
    for (const char *p = spec; *p; ) {
        if (*p == '\\' && (p[1] == 't' || p[1] == 'n')) {
            put(o, p[1] == 't' ? "\t" : "\n", 1);
            p += 2;
            continue;
        }
        if (*p == ':') {
            const char *e = strchr(p + 1, ':');
            if (e && e - p > 1 && e - p < 4) {
                char k[4];
                size_t n = (size_t)(e - p - 1);
                memcpy(k, p + 1, n);
                k[n] = '\0';
                bool known = true;
                const struct sid *s = &d->sid;
#define IS(x) (strcmp(k, x) == 0)
                if (IS("Dt"))
                    putf(o, "%c %s %s %s %s %d %d", d->type, sid_str(s, sidb), d->date, d->time,
                         d->user, d->serial, d->pred);
                else if (IS("DL"))
                    putf(o, "%05ld/%05ld/%05ld", d->ins > 99999 ? 99999 : d->ins,
                         d->del > 99999 ? 99999 : d->del, d->unch > 99999 ? 99999 : d->unch);
                else if (IS("Li"))
                    putf(o, "%05ld", d->ins > 99999 ? 99999 : d->ins);
                else if (IS("Ld"))
                    putf(o, "%05ld", d->del > 99999 ? 99999 : d->del);
                else if (IS("Lu"))
                    putf(o, "%05ld", d->unch > 99999 ? 99999 : d->unch);
                else if (IS("DT"))
                    putf(o, "%c", d->type);
                else if (IS("I"))
                    puts_(o, sid_str(s, sidb));
                else if (IS("R"))
                    putf(o, "%d", s->r);
                else if (IS("L"))
                    putf(o, "%d", s->l);
                else if (IS("B"))
                    s->b ? putf(o, "%d", s->b) : (void)0;
                else if (IS("S"))
                    s->b ? putf(o, "%d", s->s) : (void)0;
                else if (IS("D"))
                    puts_(o, d->date);
                else if (IS("Dy"))
                    putf(o, "%.2s", d->date);
                else if (IS("Dm"))
                    putf(o, "%.2s", d->date + 3);
                else if (IS("Dd"))
                    putf(o, "%.2s", d->date + 6);
                else if (IS("T"))
                    puts_(o, d->time);
                else if (IS("Th"))
                    putf(o, "%.2s", d->time);
                else if (IS("Tm"))
                    putf(o, "%.2s", d->time + 3);
                else if (IS("Ts"))
                    putf(o, "%.2s", d->time + 6);
                else if (IS("P"))
                    puts_(o, d->user);
                else if (IS("DS"))
                    putf(o, "%d", d->serial);
                else if (IS("DP"))
                    putf(o, "%d", d->pred);
                else if (IS("DI")) {
                    if (d->ninc || d->nexc || d->nign) {
                        serials(o, d->inc, d->ninc);
                        puts_(o, "/");
                        serials(o, d->exc, d->nexc);
                        puts_(o, "/");
                        serials(o, d->ign, d->nign);
                    }
                } else if (IS("Dn"))
                    serials(o, d->inc, d->ninc);
                else if (IS("Dx"))
                    serials(o, d->exc, d->nexc);
                else if (IS("Dg"))
                    serials(o, d->ign, d->nign);
                else if (IS("MR"))
                    for (int i = 0; i < d->mrs.n; i++)
                        putf(o, "%s\n", d->mrs.v[i]);
                else if (IS("C"))
                    for (int i = 0; i < d->comments.n; i++)
                        putf(o, "%s\n", d->comments.v[i]);
                else if (IS("UN")) {
                    if (!sf->users.n)
                        puts_(o, "none\n");
                    for (int i = 0; i < sf->users.n; i++)
                        putf(o, "%s\n", sf->users.v[i]);
                } else if (IS("FL"))
                    flag_list(o, sf);
                else if (IS("Y"))
                    puts_(o, or_none(flagval(sf, 't')));
                else if (IS("MF"))
                    puts_(o, yesno(flagval(sf, 'v')));
                else if (IS("MP")) {
                    const char *v = flagval(sf, 'v');
                    puts_(o, v && *v ? v : "none");
                } else if (IS("KF"))
                    puts_(o, yesno(flagval(sf, 'i')));
                else if (IS("BF"))
                    puts_(o, yesno(flagval(sf, 'b')));
                else if (IS("J"))
                    puts_(o, yesno(flagval(sf, 'j')));
                else if (IS("LK"))
                    puts_(o, or_none(flagval(sf, 'l')));
                else if (IS("Q")) {
                    const char *v = flagval(sf, 'q');
                    puts_(o, v ? v : "");
                } else if (IS("M"))
                    puts_(o, module_name(sf));
                else if (IS("FB"))
                    puts_(o, or_none(flagval(sf, 'f')));
                else if (IS("CB"))
                    puts_(o, or_none(flagval(sf, 'c')));
                else if (IS("Ds"))
                    puts_(o, or_none(flagval(sf, 'd')));
                else if (IS("ND"))
                    puts_(o, yesno(flagval(sf, 'n')));
                else if (IS("FD")) {
                    if (!sf->desc.n)
                        puts_(o, "none\n");
                    for (int i = 0; i < sf->desc.n; i++)
                        putf(o, "%s\n", sf->desc.v[i]);
                } else if (IS("BD"))
                    for (int i = 0; i < sf->body.n; i++)
                        putf(o, "%s\n", sf->body.v[i]);
                else if (IS("GB")) {
                    int *st;
                    if (applied_set(sf, d, NULL, NULL, 0, &st)) {
                        struct text t;
                        struct kwctx kw;
                        bool found = false;
                        retrieve(sf, st, &t);
                        kw_init(&kw, sf, d, st);
                        for (int i = 0; i < t.lines.n; i++) {
                            char *x = kw_expand(&kw, t.lines.v[i], i + 1, &found);
                            putf(o, "%s\n", x);
                            free(x);
                        }
                        text_free(&t);
                        free(st);
                    }
                } else if (IS("W"))
                    putf(o, "@(#)%s\t%s", module_name(sf), sid_str(s, sidb));
                else if (IS("A")) {
                    const char *y = flagval(sf, 't');
                    putf(o, "@(#)%s %s %s@(#)", y ? y : "none", module_name(sf), sid_str(s, sidb));
                } else if (IS("Z"))
                    puts_(o, "@(#)");
                else if (IS("F"))
                    puts_(o, base_name(sf->path));
                else if (IS("PN"))
                    puts_(o, sf->path);
                else
                    known = false;
#undef IS
                if (known) {
                    p = e + 1;
                    continue;
                }
            }
        }
        put(o, p, 1);
        p++;
    }
}

static void do_file(const char *path)
{
    struct sfile sf;
    if (!sfile_read(path, &sf, NULL))
        return;
    const struct delta *ref = NULL;
    if (o_r || (!o_c && o_d && !o_e && !o_l)) {
        if (o_rsid && *o_rsid) {
            struct sid s;
            int n = sid_parse(o_rsid, &s);
            if (n == 2 || n == 4) {
                ref = by_sid(&sf, &s);
                for (int i = 0; i < sf.nd && !ref && o_a; i++)
                    if (sid_eq(&sf.d[i].sid, &s))
                        ref = &sf.d[i];
            } else {
                struct delta *g;
                if (n && choose(&sf, o_rsid, false, &g))
                    ref = g;
                else if (!n)
                    err("%s: invalid SID: %s", path, o_rsid);
                if (!ref) {
                    sfile_free(&sf);
                    return;
                }
            }
            if (!ref) {
                err("%s: nonexistent SID: %s", path, o_rsid);
                sfile_free(&sf);
                return;
            }
        } else {
            /* the most recently made */
            for (int i = 0; i < sf.nd; i++)
                if ((o_a || sf.d[i].type == 'D') && (!ref || sf.d[i].serial > ref->serial))
                    ref = &sf.d[i];
        }
    }
    struct out o = { 0 };
    const char *spec = o_d ? o_d : ":Dt:\t:DL:\nMRs:\n:MR:COMMENTS:\n:C:";
    bool gb = strstr(spec, ":GB:") != NULL;
    if (!o_d && !o_r)
        putf(&o, "%s:\n\n", sf.path);
    for (int i = 0; i < sf.nd; i++) {
        const struct delta *d = &sf.d[i];
        if (d->type != 'D' && !o_a)
            continue;
        bool sel;
        if (o_e || o_l) {
            if (o_c) {
                time_t t = delta_time(d);
                sel = o_e ? t <= cutoff : t >= cutoff;
            } else {
                sel = o_e ? d->serial <= ref->serial : d->serial >= ref->serial;
            }
        } else if (o_c) {
            sel = delta_time(d) <= cutoff;
        } else if (ref) {
            sel = d == ref;
        } else {
            sel = true;
        }
        if (!sel)
            continue;
        if (gb && d->type != 'D' && !d->pred) {
            /* a removed delta with nothing before it: no text at all, and
               nothing for the file (as CSSC) */
            char sidb[64];
            err("%s: %s is removed: it has no body", path, sid_str(&d->sid, sidb));
            free(o.s);
            sfile_free(&sf);
            return;
        }
        expand_spec(&o, spec, &sf, d);
        puts_(&o, "\n");
    }
    if (o.s)
        fwrite(o.s, 1, o.n, stdout);
    free(o.s);
    sfile_free(&sf);
}

int prs_main(int argc, char **argv)
{
    struct opts o = { 1, 0, NULL };
    int c;
    while ((c = sopt(argc, argv, "ad:r;elc:", &o)) != -1) {
        switch (c) {
        case 'a': o_a = true; break;
        case 'd': o_d = o.arg; break;
        case 'r': o_r = true; o_rsid = o.arg; break;
        case 'e': o_e = true; break;
        case 'l': o_l = true; break;
        case 'c': o_c = o.arg; break;
        default: goto usage;
        }
    }
    if (o.ind >= argc || (o_e && o_l) || ((o_e || o_l) && !o_r && !o_c) || (o_c && o_r))
        goto usage;
    if (o_c && !parse_cutoff(o_c, &cutoff)) {
        err("invalid cutoff: %s", o_c);
        return 1;
    }
    bool multi;
    for_each_file(argc - o.ind, argv + o.ind, do_file, &multi);
    if (fflush(stdout) != 0)
        return 1;
    return status;
usage:
    fprintf(stderr, "usage: prs [-a] [-d dataspec] [-r[SID]] file...\n"
                    "       prs [-e|-l] -c cutoff [-d dataspec] file...\n"
                    "       prs [-e|-l] -r[SID] [-d dataspec] file...\n");
    return 2;
}
