/*
 * rmdel.c - rmdel: remove a delta from an SCCS file
 * (POSIX.1-2024, XCU rmdel)
 *
 *   rmdel -r SID file...
 */

#include "sccs.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *o_r;

static void do_file(const char *path)
{
    struct sfile sf;
    struct pfile pf = { 0 };
    char *pname = NULL;
    if (!sfile_read(path, &sf, NULL))
        return;
    if (!lock_sccs(path)) {
        sfile_free(&sf);
        return;
    }
    struct sid s;
    struct delta *d;
    int n = sid_parse(o_r, &s);
    if ((n != 2 && n != 4) || !(d = by_sid(&sf, &s))) {
        err("%s: nonexistent SID: %s", path, o_r);
        goto out;
    }
    for (int i = 0; i < sf.nd; i++)
        if (sf.d[i].type == 'D' && sf.d[i].pred == d->serial) {
            err("%s: %s is not the last delta of its branch", path, o_r);
            goto out;
        }
    pname = aux_name(path, 'p');
    if (!pfile_read(pname, &pf))
        goto out;
    for (int i = 0; i < pf.n; i++)
        if (strcmp(pf.e[i].got, o_r) == 0 || strcmp(pf.e[i].newsid, o_r) == 0) {
            err("%s: %s is being edited", path, o_r);
            goto out;
        }
    /* the user who made it, the owner of the file or of its directory */
    struct stat fst, dst;
    uid_t uid = getuid();
    char *dir = xstrdup(path), *sl = strrchr(dir, '/');
    if (sl)
        sl[sl == dir ? 1 : 0] = '\0';
    else
        strcpy(dir, ".");
    bool ok = strcmp(d->user, login_name()) == 0
        || (stat(path, &fst) == 0 && fst.st_uid == uid)
        || (stat(dir, &dst) == 0 && dst.st_uid == uid);
    free(dir);
    if (!ok) {
        err("%s: %s may not remove %s", path, login_name(), o_r);
        goto out;
    }
    /* the body without what the delta did */
    int v = d->serial;
    struct strs body = { 0 };
    int *istack = xmalloc((size_t)(sf.maxserial + 2) * sizeof *istack);
    int ni = 0;
    for (int i = 0; i < sf.body.n; i++) {
        const char *l = sf.body.v[i];
        if (l[0] == SOH) {
            int x = atoi(l + 3);
            if (l[1] == 'I') {
                istack[ni++] = x;
            } else if (l[1] == 'E') {
                int j;
                for (j = ni - 1; j >= 0 && istack[j] != x; j--)
                    ;
                if (j >= 0) {
                    memmove(istack + j, istack + j + 1, (size_t)(ni - j - 1) * sizeof *istack);
                    ni--;
                }
            }
            if (x != v)
                strs_add(&body, l);
            continue;
        }
        if (ni > 0 && istack[ni - 1] == v)
            continue;
        strs_add(&body, l);
    }
    free(istack);
    strs_free(&sf.body);
    sf.body = body;
    d->type = 'R';
    struct stat st;
    sfile_write(&sf, stat(path, &st) == 0 ? st.st_mode & 07777 : 0444);
out:
    unlock_sccs();
    free(pname);
    pfile_free(&pf);
    sfile_free(&sf);
}

int rmdel_main(int argc, char **argv)
{
    struct opts o = { 1, 0, NULL };
    int c;
    while ((c = sopt(argc, argv, "r:", &o)) != -1) {
        if (c != 'r')
            goto usage;
        o_r = o.arg;
    }
    if (!o_r || o.ind >= argc)
        goto usage;
    bool multi;
    for_each_file(argc - o.ind, argv + o.ind, do_file, &multi);
    return status;
usage:
    fprintf(stderr, "usage: rmdel -r SID file...\n");
    return 2;
}
