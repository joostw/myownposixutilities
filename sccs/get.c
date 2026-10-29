/*
 * get.c - get: get a version of an SCCS file (POSIX.1-2024, XCU get)
 *
 *   get [-begkmnlLpst] [-c cutoff] [-i list] [-r SID] [-x list] file...
 */

#include "sccs.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool o_b, o_e, o_g, o_k, o_m, o_n, o_l, o_L, o_p, o_s, o_t;
static const char *o_c, *o_i, *o_r, *o_x;
static time_t cutoff;
static bool multi;
static FILE *info;              /* the informative output, or NULL */

/* the l-file (or -L) lines */
static void summary(FILE *f, const struct sfile *sf, const int *st)
{
    char sidb[64];
    for (int i = 0; i < sf->nd; i++) {
        const struct delta *d = &sf->d[i];
        int s = st[d->serial];
        if (d->type != 'D' || !(s & (AP_APPLIED | AP_PATH | AP_INCLUDED | AP_EXCLUDED | AP_IGNORED | AP_CUTOFF)))
            continue;
        char c1 = s & AP_APPLIED ? ' ' : '*';
        char c2 = (s & AP_APPLIED) || (s & AP_IGNORED) ? ' ' : '*';
        char c3 = s & AP_INCLUDED ? 'I' : s & AP_EXCLUDED ? 'X' : s & AP_CUTOFF ? 'C' : ' ';
        fprintf(f, "%c%c%c %s\t%s %s %s\n", c1, c2, c3, sid_str(&d->sid, sidb), d->date, d->time, d->user);
        for (int k = 0; k < d->mrs.n; k++)
            fprintf(f, "\t%s\n", d->mrs.v[k]);
        for (int k = 0; k < d->comments.n; k++)
            fprintf(f, "\t%s\n", d->comments.v[k]);
        fputc('\n', f);
    }
}

static void list_sids(const struct sfile *sf, const int *st, int flag, const char *title)
{
    char sidb[64];
    bool any = false;
    for (int i = 0; i < sf->nd; i++)
        if (st[sf->d[i].serial] & flag) {
            if (!any && info)
                fprintf(info, "%s\n", title);
            any = true;
            if (info)
                fprintf(info, "%s\n", sid_str(&sf->d[i].sid, sidb));
        }
}

static bool locked_release(const struct sfile *sf, int r)
{
    const char *l = flagval(sf, 'l');
    if (!l)
        return false;
    if (strcmp(l, "a") == 0)
        return true;
    char *copy = xstrdup(l), *save = NULL;
    bool hit = false;
    for (char *t = strtok_r(copy, " ,", &save); t; t = strtok_r(NULL, " ,", &save)) {
        if (strcmp(t, "a") == 0 || atoi(t) == r)
            hit = true;
    }
    free(copy);
    return hit;
}

static void do_file(const char *path)
{
    struct sfile sf;
    struct pfile pf = { 0 };
    int *st = NULL;
    char *pname = NULL, *gname = NULL;
    bool lock = false;
    char sidb[64], nsidb[64];

    if (multi && info)
        fprintf(info, "\n%s:\n", path);
    if (!sfile_read(path, &sf, NULL))
        return;
    if (o_e) {
        if (!lock_sccs(path))
            goto out;
        lock = true;
    }
    struct delta *got;
    if (!choose(&sf, o_r, o_t, &got))
        goto out;
    if (!applied_set(&sf, got, o_i, o_x, cutoff, &st))
        goto out;

    struct sid nsid;
    pname = aux_name(path, 'p');
    if (o_e) {
        const char *login = login_name();
        if (!may_edit(&sf, login)) {
            err("%s: %s may not make deltas", path, login);
            goto out;
        }
        const char *c = flagval(&sf, 'c'), *f = flagval(&sf, 'f');
        if ((c && *c && got->sid.r > atoi(c)) || (f && *f && got->sid.r < atoi(f))) {
            err("%s: release %d is outside the floor and ceiling", path, got->sid.r);
            goto out;
        }
        if (!pfile_read(pname, &pf))
            goto out;
        struct strs reserved = { 0 };
        for (int i = 0; i < pf.n; i++) {
            if (strcmp(pf.e[i].got, sid_str(&got->sid, sidb)) == 0 && !flagval(&sf, 'j')) {
                err("%s: %s is being edited: %s %s %s %s %s", path, sidb, pf.e[i].got,
                    pf.e[i].newsid, pf.e[i].user, pf.e[i].date, pf.e[i].time);
                strs_free(&reserved);
                goto out;
            }
            strs_add(&reserved, pf.e[i].newsid);
        }
        bool br = o_b && flagval(&sf, 'b');
        new_sid(&sf, o_r, got, br, &reserved, &nsid);
        strs_free(&reserved);
        if (locked_release(&sf, nsid.r)) {
            err("%s: release %d is locked", path, nsid.r);
            goto out;
        }
    }

    /* the text */
    struct text t;
    retrieve(&sf, st, &t);
    gname = aux_name(path, 'g');
    if (!o_g && !o_p) {
        struct stat gs;
        if (stat(gname, &gs) == 0 && (gs.st_mode & 0222)) {
            err("%s: writable `%s' exists", path, gname);
            text_free(&t);
            goto out;
        }
    }
    struct kwctx k;
    kw_init(&k, &sf, got, st);
    bool expand_kw = !o_k && !o_e, found = false;
    struct strs outl = { 0 };
    if (!o_g) {
        for (int i = 0; i < t.lines.n; i++) {
            char *line = expand_kw ? kw_expand(&k, t.lines.v[i], i + 1, &found) : xstrdup(t.lines.v[i]);
            if (!expand_kw && has_keywords(line))
                found = true;
            char *full = line;
            if (o_m || o_n) {
                const struct delta *by = by_serial(&sf, t.by[i]);
                size_t n = strlen(line) + 128 + strlen(module_name(&sf));
                full = xmalloc(n);
                if (o_n && o_m)
                    snprintf(full, n, "%s\t%s\t%s", module_name(&sf), sid_str(&by->sid, sidb), line);
                else if (o_m)
                    snprintf(full, n, "%s\t%s", sid_str(&by->sid, sidb), line);
                else
                    snprintf(full, n, "%s\t%s", module_name(&sf), line);
                free(line);
            }
            strs_add(&outl, full);
            free(full);
        }
        if (expand_kw && !found) {
            const char *iflag = flagval(&sf, 'i');
            if (iflag) {
                err("%s: no id keywords", path);
                strs_free(&outl);
                text_free(&t);
                goto out;
            }
            warn("warning: %s: No id keywords.", path);
        }
        if (expand_kw && found) {
            const char *iflag = flagval(&sf, 'i');
            if (iflag && *iflag) {
                bool hit = false;
                for (int i = 0; i < t.lines.n && !hit; i++)
                    hit = strstr(t.lines.v[i], iflag) != NULL;
                if (!hit) {
                    err("%s: the id keywords do not match the i flag", path);
                    strs_free(&outl);
                    text_free(&t);
                    goto out;
                }
            }
        }
    }

    /* the report */
    list_sids(&sf, st, AP_INCLUDED, "Included:");
    list_sids(&sf, st, AP_EXCLUDED, "Excluded:");
    if (info) {
        fprintf(info, "%s\n", sid_str(&got->sid, sidb));
        if (o_e)
            fprintf(info, "new delta %s\n", sid_str(&nsid, nsidb));
        if (!o_g)
            fprintf(info, "%d lines\n", t.lines.n);
    }

    /* the g-file, or the standard output */
    if (!o_g) {
        FILE *g = stdout;
        if (!o_p) {
            unlink(gname);
            mode_t m = expand_kw ? 0444 : 0644;
            int fd = open(gname, O_WRONLY | O_CREAT | O_TRUNC, m);
            if (fd < 0 || !(g = fdopen(fd, "w"))) {
                err("%s: %s", gname, strerror(errno));
                strs_free(&outl);
                text_free(&t);
                goto out;
            }
        }
        for (int i = 0; i < outl.n; i++)
            fprintf(g, "%s\n", outl.v[i]);
        if (g != stdout) {
            if (fclose(g) != 0) {
                err("%s: %s", gname, strerror(errno));
                strs_free(&outl);
                text_free(&t);
                goto out;
            }
        } else if (fflush(stdout) != 0) {
            err("standard output: %s", strerror(errno));
        }
    }
    strs_free(&outl);
    text_free(&t);

    if (o_l) {
        char *lname = aux_name(path, 'l');
        /* in the current directory */
        char *lb = xstrdup(base_name(lname));
        unlink(lb);
        FILE *l = fopen(lb, "w");
        if (!l) {
            err("%s: %s", lb, strerror(errno));
        } else {
            summary(l, &sf, st);
            if (fclose(l) != 0)
                err("%s: %s", lb, strerror(errno));
            chmod(lb, 0444);
        }
        free(lname);
        free(lb);
    }
    if (o_L)
        summary(stdout, &sf, st);

    if (o_e) {
        char d[9], tm[9];
        now_strings(d, tm);
        pf.e = xrealloc(pf.e, (size_t)(pf.n + 1) * sizeof *pf.e);
        struct pentry *e = &pf.e[pf.n++];
        e->got = xstrdup(sid_str(&got->sid, sidb));
        e->newsid = xstrdup(sid_str(&nsid, nsidb));
        e->user = xstrdup(login_name());
        e->date = xstrdup(d);
        e->time = xstrdup(tm);
        e->ilist = o_i ? xstrdup(o_i) : NULL;
        e->xlist = o_x ? xstrdup(o_x) : NULL;
        pfile_write(pname, &pf);
    }
out:
    if (lock)
        unlock_sccs();
    pfile_free(&pf);
    free(st);
    free(pname);
    free(gname);
    sfile_free(&sf);
}

int get_main(int argc, char **argv)
{
    struct opts o = { 1, 0, NULL };
    int c;
    while ((c = sopt(argc, argv, "begkmnlLpstc:i:r:x:", &o)) != -1) {
        switch (c) {
        case 'b': o_b = true; break;
        case 'e': o_e = true; break;
        case 'g': o_g = true; break;
        case 'k': o_k = true; break;
        case 'm': o_m = true; break;
        case 'n': o_n = true; break;
        case 'l': o_l = true; break;
        case 'L': o_L = true; break;
        case 'p': o_p = true; break;
        case 's': o_s = true; break;
        case 't': o_t = true; break;
        case 'c': o_c = o.arg; break;
        case 'i': o_i = o.arg; break;
        case 'r': o_r = o.arg; break;
        case 'x': o_x = o.arg; break;
        default: goto usage;
        }
    }
    if (o.ind >= argc)
        goto usage;
    if (o_c && !parse_cutoff(o_c, &cutoff)) {
        err("invalid cutoff: %s", o_c);
        return 1;
    }
    if (o_r && !sid_parse(o_r, &(struct sid){ 0, 0, 0, 0 })) {
        err("invalid SID: %s", o_r);
        return 1;
    }
    info = o_s ? NULL : (o_p || o_L) ? stderr : stdout;
    for_each_file(argc - o.ind, argv + o.ind, do_file, &multi);
    if (fflush(stdout) != 0)
        return 1;
    return status;
usage:
    fprintf(stderr, "usage: get [-begkmnlLpst] [-c cutoff] [-i list] [-r SID] [-x list] file...\n");
    return 2;
}
