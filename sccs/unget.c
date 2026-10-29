/*
 * unget.c - unget: undo a previous get of an SCCS file
 * (POSIX.1-2024, XCU unget)
 *
 *   unget [-ns] [-r SID] file...
 */

#include "sccs.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool o_n, o_s, multi;
static const char *o_r;

static void do_file(const char *path)
{
    if (multi && !o_s)
        printf("\n%s:\n", path);
    if (access(path, R_OK) != 0) {
        err("%s: %s", path, strerror(errno));
        return;
    }
    if (!lock_sccs(path))
        return;
    char *pname = aux_name(path, 'p');
    struct pfile pf;
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
    char *newsid = xstrdup(pf.e[which].newsid);
    struct pentry *e = &pf.e[which];
    free(e->got), free(e->newsid), free(e->user), free(e->date), free(e->time);
    free(e->ilist), free(e->xlist);
    memmove(pf.e + which, pf.e + which + 1, (size_t)(pf.n - which - 1) * sizeof *pf.e);
    pf.n--;
    if (pfile_write(pname, &pf)) {
        if (!o_n) {
            char *g = aux_name(path, 'g');
            if (unlink(g) != 0 && errno != ENOENT)
                err("%s: %s", g, strerror(errno));
            free(g);
        }
        if (!o_s)
            printf("%s\n", newsid);
    }
    free(newsid);
out:
    unlock_sccs();
    pfile_free(&pf);
    free(pname);
}

int unget_main(int argc, char **argv)
{
    struct opts o = { 1, 0, NULL };
    int c;
    while ((c = sopt(argc, argv, "nsr:", &o)) != -1) {
        switch (c) {
        case 'n': o_n = true; break;
        case 's': o_s = true; break;
        case 'r': o_r = o.arg; break;
        default: goto usage;
        }
    }
    if (o.ind >= argc)
        goto usage;
    if (o_r && !sid_parse(o_r, &(struct sid){ 0, 0, 0, 0 })) {
        err("invalid SID: %s", o_r);
        return 1;
    }
    for_each_file(argc - o.ind, argv + o.ind, do_file, &multi);
    if (fflush(stdout) != 0)
        return 1;
    return status;
usage:
    fprintf(stderr, "usage: unget [-ns] [-r SID] file...\n");
    return 2;
}
