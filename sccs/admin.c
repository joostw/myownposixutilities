/*
 * admin.c - admin: create and administer SCCS files (POSIX.1-2024, XCU admin)
 *
 *   admin -i[name] [-n] [-a login] [-d flag] [-e login] [-f flag]
 *         [-m mrlist] [-r rel] [-t[name]] [-y[comment]] newfile
 *   admin -n [-a login] [-d flag] [-e login] [-f flag] [-m mrlist]
 *         [-t[name]] [-y[comment]] newfile...
 *   admin [-a login] [-d flag] [-m mrlist] [-r rel] [-t[name]] file...
 *   admin -h file...
 *   admin -z file...
 */

#include "sccs.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool o_n, o_h, o_z;
static const char *o_i, *o_t, *o_y, *o_m, *o_r;
static struct strs adds, erases, setf, delf;

static bool valid_flag(const char *f, bool deleting)
{
    if (!islower((unsigned char)f[0]))
        return false;
    const char *v = f + 1;
    switch (f[0]) {
    case 'b': case 'j': case 'n':
        return !*v;
    case 'i': case 'v': case 'q': case 'm': case 't':
        return true;
    case 'c': case 'f':
        if (deleting)
            return !*v;
        for (const char *p = v; *p; p++)
            if (!isdigit((unsigned char)*p))
                return false;
        return *v && atoi(v) >= 1 && atoi(v) <= 9999 && (f[0] == 'c' || atoi(v) < 9999);
    case 'd':
        return deleting ? !*v : sid_parse(v, &(struct sid){ 0, 0, 0, 0 }) != 0;
    case 'l':
        if (deleting && !*v)
            return true;
        if (strcmp(v, "a") == 0)
            return true;
        for (const char *p = v; *p; p++)
            if (!isdigit((unsigned char)*p) && *p != ',' && *p != ' ')
                return false;
        return *v != '\0';
    }
    return false;
}

/* the lock list as a set of numbers, "a" for all */
static char *merge_locks(const char *old, const char *add, const char *del)
{
    if ((add && strcmp(add, "a") == 0))
        return xstrdup("a");
    if (del && (strcmp(del, "a") == 0 || !*del))
        return NULL;
    if (old && strcmp(old, "a") == 0 && !del)
        return xstrdup("a");
    bool set[10000];
    memset(set, 0, sizeof set);
    const char *lists[2] = { old && strcmp(old, "a") ? old : "", add ? add : "" };
    for (int k = 0; k < 2; k++)
        for (const char *p = lists[k]; *p; ) {
            if (isdigit((unsigned char)*p)) {
                int r = atoi(p);
                if (r > 0 && r < 10000)
                    set[r] = true;
                while (isdigit((unsigned char)*p))
                    p++;
            } else {
                p++;
            }
        }
    if (del)
        for (const char *p = del; *p; ) {
            if (isdigit((unsigned char)*p)) {
                int r = atoi(p);
                if (r > 0 && r < 10000)
                    set[r] = false;
                while (isdigit((unsigned char)*p))
                    p++;
            } else {
                p++;
            }
        }
    char buf[60000];
    size_t n = 0;
    buf[0] = '\0';
    for (int r = 1; r < 10000; r++)
        if (set[r])
            n += (size_t)snprintf(buf + n, sizeof buf - n, "%s%d", n ? " " : "", r);
    return n ? xstrdup(buf) : NULL;
}

static void apply_flags(struct sfile *sf)
{
    for (int i = 0; i < setf.n; i++) {
        const char *f = setf.v[i];
        int c = f[0] - 'a';
        if (f[0] == 'l') {
            char *m = merge_locks(sf->flag[c], f + 1, NULL);
            free(sf->flag[c]);
            sf->flag[c] = m;
            continue;
        }
        free(sf->flag[c]);
        sf->flag[c] = xstrdup(f + 1);
    }
    for (int i = 0; i < delf.n; i++) {
        const char *f = delf.v[i];
        int c = f[0] - 'a';
        if (f[0] == 'l' && f[1]) {
            char *m = merge_locks(sf->flag[c], NULL, f + 1);
            free(sf->flag[c]);
            sf->flag[c] = m;
            continue;
        }
        free(sf->flag[c]);
        sf->flag[c] = NULL;
    }
}

static void apply_users(struct sfile *sf)
{
    /* the new names first, in order, then the old ones (as CSSC) */
    struct strs u = { 0 };
    for (int i = 0; i < adds.n; i++) {
        bool have = false;
        for (int k = 0; k < sf->users.n; k++)
            if (strcmp(sf->users.v[k], adds.v[i]) == 0)
                have = true;
        for (int k = 0; k < u.n; k++)
            if (strcmp(u.v[k], adds.v[i]) == 0)
                have = true;
        if (!have)
            strs_add(&u, adds.v[i]);
    }
    for (int k = 0; k < sf->users.n; k++)
        strs_add(&u, sf->users.v[k]);
    strs_free(&sf->users);
    sf->users = u;
    for (int i = 0; i < erases.n; i++)
        for (int k = 0; k < sf->users.n; k++)
            if (strcmp(sf->users.v[k], erases.v[i]) == 0) {
                free(sf->users.v[k]);
                memmove(sf->users.v + k, sf->users.v + k + 1, (size_t)(sf->users.n - k - 1) * sizeof *sf->users.v);
                sf->users.n--;
                k--;
            }
}

static bool read_lines(FILE *f, struct strs *out, const char *name)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, f)) > 0) {
        if (line[n - 1] == '\n')
            line[--n] = '\0';
        if (line[0] == SOH) {
            err("%s: a line starts with SOH", name);
            free(line);
            return false;
        }
        strs_add(out, line);
    }
    free(line);
    return !ferror(f);
}

static bool desc_text(struct sfile *sf)
{
    strs_free(&sf->desc);
    if (!o_t || !*o_t)
        return true;
    FILE *f = fopen(o_t, "r");
    if (!f) {
        err("%s: %s", o_t, strerror(errno));
        return false;
    }
    bool ok = read_lines(f, &sf->desc, o_t);
    fclose(f);
    return ok;
}

static void create(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        err("%s: the file exists", path);
        return;
    }
    if (strncmp(base_name(path), "s.", 2) != 0) {
        err("%s: not an SCCS file name", path);
        return;
    }
    struct sfile sf;
    memset(&sf, 0, sizeof sf);
    sf.path = xstrdup(path);
    struct strs text = { 0 };
    if (o_i) {
        FILE *f = *o_i ? fopen(o_i, "r") : stdin;
        if (!f) {
            err("%s: %s", o_i, strerror(errno));
            goto out;
        }
        bool ok = read_lines(f, &text, *o_i ? o_i : "standard input");
        if (f != stdin)
            fclose(f);
        if (!ok)
            goto out;
    }
    if (!desc_text(&sf))
        goto out;
    apply_users(&sf);
    apply_flags(&sf);
    struct delta d;
    memset(&d, 0, sizeof d);
    d.type = 'D';
    d.sid = (struct sid){ 1, 1, 0, 0 };
    if (o_r) {
        struct sid s;
        int k = sid_parse(o_r, &s);
        if (k == 0 || k > 2) {
            err("%s: invalid initial SID: %s", path, o_r);
            goto out;
        }
        d.sid = (struct sid){ s.r, k == 2 ? s.l : 1, 0, 0 };
    }
    now_strings(d.date, d.time);
    d.user = xstrdup(login_name());
    d.serial = 1;
    d.ins = text.n;
    if (o_m) {
        if (!sf.flag['v' - 'a']) {
            err("%s: MRs are not allowed without the v flag", path);
            free(d.user);
            goto out;
        }
        split_mrs(o_m, &d.mrs);
        if (!check_mrs(&sf, &d.mrs)) {
            err("%s: the MRs are not valid", path);
            free(d.user);
            strs_free(&d.mrs);
            goto out;
        }
    } else if (sf.flag['v' - 'a']) {
        err("%s: MRs are required with the v flag (-m)", path);
        free(d.user);
        goto out;
    }
    if (o_y) {
        const char *p = o_y;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t n = nl ? (size_t)(nl - p) : strlen(p);
            char *c = xstrndup(p, n);
            strs_add(&d.comments, c);
            free(c);
            p += n + (nl ? 1 : 0);
        }
    } else {
        char c[256];
        snprintf(c, sizeof c, "date and time created %s %s by %s", d.date, d.time, d.user);
        strs_add(&d.comments, c);
    }
    sf.d = xmalloc(sizeof *sf.d);
    sf.d[0] = d;
    sf.nd = 1;
    sf.maxserial = 1;
    if (!sf.flag['e' - 'a'])
        sf.flag['e' - 'a'] = xstrdup("0");
    char ctl[16];
    snprintf(ctl, sizeof ctl, "%cI 1", SOH);
    strs_add(&sf.body, ctl);
    bool kw = false;
    for (int i = 0; i < text.n; i++) {
        strs_add(&sf.body, text.v[i]);
        kw |= has_keywords(text.v[i]);
    }
    snprintf(ctl, sizeof ctl, "%cE 1", SOH);
    strs_add(&sf.body, ctl);
    if (o_i && !kw) {
        if (sf.flag['i' - 'a']) {
            err("%s: no id keywords", path);
            goto out;
        }
        warn("warning: %s: No id keywords.", path);
    }
    if (!lock_sccs(path))
        goto out;
    sfile_write(&sf, 0444);
    unlock_sccs();
out:
    strs_free(&text);
    sfile_free(&sf);
}

static void change(const char *path)
{
    struct sfile sf;
    bool corrupt;
    sccs_ignore_sum(o_h || o_z);
    if (!sfile_read(path, &sf, &corrupt)) {
        sccs_ignore_sum(false);
        return;
    }
    sccs_ignore_sum(false);
    if (o_h) {
        if (sf.sum_stored != sf.sum_computed)
            err("%s: corrupted file: the checksum is %05u, not %05u", path, sf.sum_computed, sf.sum_stored);
        sfile_free(&sf);
        return;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        err("%s: %s", path, strerror(errno));
        sfile_free(&sf);
        return;
    }
    if (!lock_sccs(path)) {
        sfile_free(&sf);
        return;
    }
    if (!o_z) {
        if (o_t && !desc_text(&sf))
            goto out;
        apply_users(&sf);
        apply_flags(&sf);
    }
    sfile_write(&sf, st.st_mode & 07777);
out:
    unlock_sccs();
    sfile_free(&sf);
}

static void do_file(const char *path)
{
    if (o_n || o_i)
        create(path);
    else
        change(path);
}

int admin_main(int argc, char **argv)
{
    struct opts o = { 1, 0, NULL };
    int c;
    bool bad = false;
    while ((c = sopt(argc, argv, "i;nhza:d:e:f:m:r:t;y;", &o)) != -1) {
        switch (c) {
        case 'i': o_i = o.arg; o_n = true; break;
        case 'n': o_n = true; break;
        case 'h': o_h = true; break;
        case 'z': o_z = true; break;
        case 'a': strs_add(&adds, o.arg); break;
        case 'e': strs_add(&erases, o.arg); break;
        case 'f':
            if (!valid_flag(o.arg, false)) {
                warn("invalid flag: %s", o.arg);
                bad = true;
            }
            strs_add(&setf, o.arg);
            break;
        case 'd':
            if (!valid_flag(o.arg, true)) {
                warn("invalid flag: %s", o.arg);
                bad = true;
            }
            strs_add(&delf, o.arg);
            break;
        case 'm': o_m = o.arg; break;
        case 'r': o_r = o.arg; break;
        case 't': o_t = o.arg; break;
        case 'y': o_y = o.arg; break;
        default: bad = true;
        }
    }
    if (bad)
        return 1;
    int nfiles = argc - o.ind;
    if (nfiles == 0)
        goto usage;
    if ((o_h || o_z) && (o_n || o_i || adds.n || erases.n || setf.n || delf.n || o_t || o_y || o_m || o_r))
        goto usage;
    if (o_i && nfiles > 1) {
        warn("-i takes one file only");
        goto usage;
    }
    if (!o_n && (o_r || o_m || o_y)) {
        warn("-r, -m and -y are only for new files");
        goto usage;
    }
    if (o_r && !o_i) {
        warn("-r only with -i");
        goto usage;
    }
    if (o_n && (o_h || o_z))
        goto usage;
    bool multi;
    for_each_file(nfiles, argv + o.ind, do_file, &multi);
    return status;
usage:
    fprintf(stderr, "usage: admin -i[name] [-n] [-a login] [-d flag] [-e login] [-f flag] [-m mrlist]\n"
                    "             [-r rel] [-t[name]] [-y[comment]] newfile\n"
                    "       admin -n [-a login] [-d flag] [-e login] [-f flag] [-m mrlist] [-t[name]]\n"
                    "             [-y[comment]] newfile...\n"
                    "       admin [-a login] [-d flag] [-e login] [-f flag] [-t[name]] file...\n"
                    "       admin -h file...\n"
                    "       admin -z file...\n");
    return 2;
}
