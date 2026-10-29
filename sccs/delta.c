/*
 * delta.c - delta: make a delta (change) to an SCCS file
 * (POSIX.1-2024, XCU delta)
 *
 *   delta [-nps] [-g list] [-m mrlist] [-r SID] [-y[comment]] file...
 */

#include "sccs.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool o_n, o_p, o_s;
static const char *o_g, *o_m, *o_r, *o_y;
static bool stdin_names;
static struct strs mrs, comments;
static bool asked;              /* the MRs and comments have been read */

static void ask(const struct sfile *sf)
{
    if (asked)
        return;
    asked = true;
    if (flagval(sf, 'v') && !o_m && !stdin_names) {
        struct strs l = { 0 };
        read_continued("MRs? ", &l);
        for (int i = 0; i < l.n; i++)
            split_mrs(l.v[i], &mrs);
        strs_free(&l);
    }
    if (o_y) {
        const char *p = o_y;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t n = nl ? (size_t)(nl - p) : strlen(p);
            char *c = xstrndup(p, n);
            strs_add(&comments, c);
            free(c);
            p += n + (nl ? 1 : 0);
        }
    } else if (!stdin_names) {
        read_continued("comments? ", &comments);
    }
}

/* a control line */
static void ctl(struct strs *b, char c, int serial)
{
    char s[32];
    snprintf(s, sizeof s, "%c%c %d", SOH, c, serial);
    strs_add(b, s);
}

/* write the differences as diff writes them (normal format) */
static void print_diff(char **a, int na, char **b, int nb, const bool *keep, const bool *isnew)
{
    int i = 0, j = 0;
    while (i < na || j < nb) {
        if (i < na && j < nb && keep[i] && !isnew[j]) {
            i++, j++;
            continue;
        }
        int i0 = i, j0 = j;
        while (i < na && !keep[i])
            i++;
        while (j < nb && isnew[j])
            j++;
        char ra[64], rb[64];
        if (i - i0 > 1)
            snprintf(ra, sizeof ra, "%d,%d", i0 + 1, i);
        else
            snprintf(ra, sizeof ra, "%d", i0 == i ? i0 : i0 + 1);
        if (j - j0 > 1)
            snprintf(rb, sizeof rb, "%d,%d", j0 + 1, j);
        else
            snprintf(rb, sizeof rb, "%d", j0 == j ? j0 : j0 + 1);
        printf("%s%c%s\n", ra, i == i0 ? 'a' : j == j0 ? 'd' : 'c', rb);
        for (int k = i0; k < i; k++)
            printf("< %s\n", a[k]);
        if (i > i0 && j > j0)
            printf("---\n");
        for (int k = j0; k < j; k++)
            printf("> %s\n", b[k]);
    }
}

static void do_file(const char *path)
{
    struct sfile sf;
    struct pfile pf = { 0 };
    char *pname = NULL, *gname = NULL;
    int *st = NULL;
    struct text old = { 0 };
    struct strs neu = { 0 };
    bool *keep = NULL, *isnew = NULL;
    char sidb[64];

    if (!sfile_read(path, &sf, NULL))
        return;
    if (!lock_sccs(path)) {
        sfile_free(&sf);
        return;
    }
    pname = aux_name(path, 'p');
    if (!pfile_read(pname, &pf))
        goto out;
    const char *login = login_name();
    int which = -1;
    for (int i = 0; i < pf.n; i++) {
        if (strcmp(pf.e[i].user, login) != 0)
            continue;
        if (o_r && strcmp(pf.e[i].got, o_r) != 0 && strcmp(pf.e[i].newsid, o_r) != 0)
            continue;
        if (which >= 0) {
            err("%s: more than one edit by %s: name the SID with -r", path, login);
            goto out;
        }
        which = i;
    }
    if (which < 0) {
        err("%s: no edit by %s%s%s", path, login, o_r ? " of " : "", o_r ? o_r : "");
        goto out;
    }
    struct pentry *pe = &pf.e[which];
    if (!may_edit(&sf, login)) {
        err("%s: %s may not make deltas", path, login);
        goto out;
    }
    struct sid gsid, nsid;
    struct delta *got;
    if (!sid_parse(pe->got, &gsid) || !sid_parse(pe->newsid, &nsid) || !(got = by_sid(&sf, &gsid))) {
        err("%s: the p-file names a delta that is not there: %s", path, pe->got);
        goto out;
    }
    if (by_sid(&sf, &nsid)) {
        err("%s: the delta %s exists already", path, pe->newsid);
        goto out;
    }
    if (flagval(&sf, 'v') == NULL && o_m) {
        err("%s: MRs are not allowed (no v flag)", path);
        goto out;
    }

    /* the new text */
    gname = aux_name(path, 'g');
    FILE *g = fopen(gname, "r");
    if (!g) {
        err("%s: %s", gname, strerror(errno));
        goto out;
    }
    {
        char *line = NULL;
        size_t cap = 0;
        ssize_t n;
        bool bad = false;
        while ((n = getline(&line, &cap, g)) > 0) {
            if (line[n - 1] == '\n')
                line[--n] = '\0';
            if (line[0] == SOH)
                bad = true;
            strs_add(&neu, line);
        }
        free(line);
        fclose(g);
        if (bad) {
            err("%s: a line starts with SOH", gname);
            goto out;
        }
    }
    bool kw = false;
    for (int i = 0; i < neu.n && !kw; i++)
        kw = has_keywords(neu.v[i]);
    if (!kw) {
        if (flagval(&sf, 'i')) {
            err("%s: no id keywords", gname);
            goto out;
        }
        warn("warning: %s: No id keywords.", gname);
    }

    ask(&sf);
    if (flagval(&sf, 'v')) {
        if (mrs.n == 0) {
            err("%s: MRs are required", path);
            goto out;
        }
        if (!check_mrs(&sf, &mrs)) {
            err("%s: the MRs are not valid", path);
            goto out;
        }
    }

    /* the old text: the version edited */
    if (!applied_set(&sf, got, pe->ilist, pe->xlist, 0, &st))
        goto out;
    retrieve(&sf, st, &old);
    keep = xmalloc((size_t)old.lines.n + 1);
    isnew = xmalloc((size_t)neu.n + 1);
    ldiff(old.lines.v, old.lines.n, neu.v, neu.n, keep, isnew);
    long ins = 0, del = 0, unch = 0;
    for (int i = 0; i < old.lines.n; i++)
        keep[i] ? unch++ : del++;
    for (int j = 0; j < neu.n; j++)
        ins += isnew[j];

    /* the new deltas: null deltas for skipped releases (the n flag), then
       this one */
    int serial = sf.maxserial;
    int pred = got->serial;
    struct delta *nd = NULL;
    int nnew = 0;
    if (flagval(&sf, 'n') && !nsid.b && nsid.l == 1 && nsid.r > got->sid.r + 1) {
        for (int r = got->sid.r + 1; r < nsid.r; r++) {
            struct delta d;
            memset(&d, 0, sizeof d);
            d.type = 'D';
            d.sid = (struct sid){ r, 1, 0, 0 };
            now_strings(d.date, d.time);
            d.user = xstrdup(login);
            d.serial = ++serial;
            d.pred = pred;
            d.unch = old.lines.n;
            strs_add(&d.comments, "AUTO NULL DELTA");
            pred = d.serial;
            nd = xrealloc(nd, (size_t)(nnew + 1) * sizeof *nd);
            nd[nnew++] = d;
        }
    }
    struct delta d;
    memset(&d, 0, sizeof d);
    d.type = 'D';
    d.sid = nsid;
    now_strings(d.date, d.time);
    d.user = xstrdup(login);
    d.serial = ++serial;
    d.pred = pred;
    d.ins = ins, d.del = del, d.unch = unch;
    if (pe->ilist && !sid_list(&sf, pe->ilist, &d.inc, &d.ninc))
        goto out;
    if (pe->xlist && !sid_list(&sf, pe->xlist, &d.exc, &d.nexc))
        goto out;
    if (o_g && !sid_list(&sf, o_g, &d.ign, &d.nign))
        goto out;
    for (int i = 0; i < mrs.n; i++)
        strs_add(&d.mrs, mrs.v[i]);
    for (int i = 0; i < comments.n; i++)
        strs_add(&d.comments, comments.v[i]);
    nd = xrealloc(nd, (size_t)(nnew + 1) * sizeof *nd);
    nd[nnew++] = d;

    /* the new body: deletions around the old lines that go, insertions
       just before the next line of the old version after the line they
       follow (or at the end), as CSSC places them */
    struct strs body = { 0 };
    {
        /* the new lines go in groups after k old lines (deletions first) */
        int nold = old.lines.n;
        int *gstart = xmalloc(((size_t)nold + 1) * sizeof *gstart);
        int *gend = xmalloc(((size_t)nold + 1) * sizeof *gend);
        for (int k = 0; k <= nold; k++)
            gstart[k] = gend[k] = 0;
        for (int i = 0, j = 0; i < nold || j < neu.n; ) {
            if (i < nold && !keep[i]) {
                i++;
            } else if (j < neu.n && isnew[j]) {
                if (gstart[i] == gend[i])
                    gstart[i] = j;
                gend[i] = j + 1;
                j++;
            } else {
                i++, j++;
            }
        }
        int *istack = xmalloc((size_t)(sf.maxserial + 2) * sizeof *istack);
        int *dcount = xmalloc((size_t)(sf.maxserial + 2) * sizeof *dcount);
        memset(dcount, 0, (size_t)(sf.maxserial + 2) * sizeof *dcount);
        int ni = 0, dapp = 0, k = 0, done = -1;
        bool dopen = false;
        for (int bi = 0; bi <= sf.body.n; bi++) {
            const char *s = bi < sf.body.n ? sf.body.v[bi] : NULL;
            if (s && s[0] == SOH) {
                int v = atoi(s + 3);
                if (s[1] == 'I') {
                    istack[ni++] = v;
                } else if (s[1] == 'D') {
                    if (dcount[v]++ == 0 && (st[v] & AP_APPLIED))
                        dapp++;
                } else {
                    int jx;
                    for (jx = ni - 1; jx >= 0 && istack[jx] != v; jx--)
                        ;
                    if (jx >= 0) {
                        memmove(istack + jx, istack + jx + 1, (size_t)(ni - jx - 1) * sizeof *istack);
                        ni--;
                    } else if (dcount[v] > 0 && --dcount[v] == 0 && (st[v] & AP_APPLIED)) {
                        dapp--;
                    }
                }
                strs_add(&body, s);
                continue;
            }
            bool visible = s && ni > 0 && (st[istack[ni - 1]] & AP_APPLIED) && dapp == 0;
            if (s && !visible) {
                /* a line of another version: as it is (a deletion block
                   stays open around it) */
                strs_add(&body, s);
                continue;
            }
            bool deleting = visible && !keep[k];
            if (dopen && !deleting) {
                ctl(&body, 'E', d.serial);
                dopen = false;
            }
            if (done < k && gend[k] > gstart[k] && !deleting) {
                ctl(&body, 'I', d.serial);
                for (int j = gstart[k]; j < gend[k]; j++)
                    strs_add(&body, neu.v[j]);
                ctl(&body, 'E', d.serial);
                done = k;
            }
            if (!s)
                break;
            if (deleting && !dopen) {
                ctl(&body, 'D', d.serial);
                dopen = true;
            }
            strs_add(&body, s);
            if (visible)
                k++;
        }
        if (dopen)
            ctl(&body, 'E', d.serial);
        free(istack);
        free(dcount);
        free(gstart);
        free(gend);
    }

    /* the delta table: the new ones first */
    struct delta *all = xmalloc((size_t)(sf.nd + nnew) * sizeof *all);
    for (int i = 0; i < nnew; i++)
        all[i] = nd[nnew - 1 - i];
    memcpy(all + nnew, sf.d, (size_t)sf.nd * sizeof *all);
    free(sf.d);
    free(nd);
    sf.d = all;
    sf.nd += nnew;
    sf.maxserial = serial;
    strs_free(&sf.body);
    sf.body = body;

    struct stat sst;
    mode_t mode = stat(path, &sst) == 0 ? sst.st_mode & 07777 : 0444;
    if (o_p)
        print_diff(old.lines.v, old.lines.n, neu.v, neu.n, keep, isnew);
    if (!sfile_write(&sf, mode))
        goto out;
    /* the p-file entry goes */
    free(pe->got), free(pe->newsid), free(pe->user), free(pe->date), free(pe->time);
    free(pe->ilist), free(pe->xlist);
    memmove(pf.e + which, pf.e + which + 1, (size_t)(pf.n - which - 1) * sizeof *pf.e);
    pf.n--;
    pfile_write(pname, &pf);
    if (!o_n && unlink(gname) != 0)
        err("%s: %s", gname, strerror(errno));
    if (!o_s)
        printf("%s\n%ld inserted\n%ld deleted\n%ld unchanged\n", sid_str(&nsid, sidb), ins, del, unch);
out:
    unlock_sccs();
    free(keep);
    free(isnew);
    text_free(&old);
    strs_free(&neu);
    free(st);
    free(pname);
    free(gname);
    pfile_free(&pf);
    sfile_free(&sf);
}

int delta_main(int argc, char **argv)
{
    struct opts o = { 1, 0, NULL };
    int c;
    while ((c = sopt(argc, argv, "npsg:m:r:y;", &o)) != -1) {
        switch (c) {
        case 'n': o_n = true; break;
        case 'p': o_p = true; break;
        case 's': o_s = true; break;
        case 'g': o_g = o.arg; break;
        case 'm': o_m = o.arg; break;
        case 'r': o_r = o.arg; break;
        case 'y': o_y = o.arg; break;
        default: goto usage;
        }
    }
    if (o.ind >= argc)
        goto usage;
    stdin_names = argc - o.ind == 1 && strcmp(argv[o.ind], "-") == 0;
    if (stdin_names && !o_y) {
        warn("-y is needed with -");
        goto usage;
    }
    if (o_m)
        split_mrs(o_m, &mrs);
    if (o_r && !sid_parse(o_r, &(struct sid){ 0, 0, 0, 0 })) {
        err("invalid SID: %s", o_r);
        return 1;
    }
    bool multi;
    for_each_file(argc - o.ind, argv + o.ind, do_file, &multi);
    if (fflush(stdout) != 0)
        return 1;
    return status;
usage:
    fprintf(stderr, "usage: delta [-nps] [-g list] [-m mrlist] [-r SID] [-y[comment]] file...\n");
    return 2;
}
