/*
 * sact.c - sact: print the current SCCS file-editing activity
 * (POSIX.1-2024, XCU sact)
 *
 *   sact file...
 */

#include "sccs.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool multi;

static void do_file(const char *path)
{
    if (multi)
        printf("\n%s:\n", path);
    if (access(path, R_OK) != 0) {
        err("%s: %s", path, strerror(errno));
        return;
    }
    char *pname = aux_name(path, 'p');
    struct pfile pf;
    if (pfile_read(pname, &pf))
        for (int i = 0; i < pf.n; i++)
            printf("%s %s %s %s %s\n", pf.e[i].got, pf.e[i].newsid, pf.e[i].user, pf.e[i].date,
                   pf.e[i].time);
    pfile_free(&pf);
    free(pname);
}

int sact_main(int argc, char **argv)
{
    int first = 1;
    if (argc > 1 && strcmp(argv[1], "--") == 0)
        first = 2;
    if (first >= argc) {
        fprintf(stderr, "usage: sact file...\n");
        return 2;
    }
    for_each_file(argc - first, argv + first, do_file, &multi);
    if (fflush(stdout) != 0)
        return 1;
    return status;
}
