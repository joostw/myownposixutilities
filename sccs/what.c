/*
 * what.c - what: identify SCCS files (POSIX.1-2024, XCU what)
 *
 *   what [-s] file...
 *
 * Each string after "@(#)", up to ", >, newline, \, NUL or the end.
 */

#include "sccs.h"

#include <errno.h>
#include <string.h>

int what_main(int argc, char **argv)
{
    struct opts o = { 1, 0, NULL };
    bool o_s = false, found = false;
    int c;
    while ((c = sopt(argc, argv, "s", &o)) != -1) {
        if (c != 's') {
            fprintf(stderr, "usage: what [-s] file...\n");
            return 2;
        }
        o_s = true;
    }
    if (o.ind >= argc) {
        fprintf(stderr, "usage: what [-s] file...\n");
        return 2;
    }
    int ret = 0;
    for (int i = o.ind; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) {
            err("%s: %s", argv[i], strerror(errno));
            ret = 1;
            continue;
        }
        printf("%s:\n", argv[i]);
        static const char pat[] = "@(#)";
        int m = 0, ch;
        while ((ch = getc(f)) != EOF) {
            if (ch == pat[m]) {
                if (++m < 4)
                    continue;
                m = 0;
                putchar('\t');
                while ((ch = getc(f)) != EOF && ch != '"' && ch != '>' && ch != '\n'
                       && ch != '\\' && ch != '\0')
                    putchar(ch);
                putchar('\n');
                found = true;
                if (o_s)
                    break;
                continue;
            }
            m = ch == pat[0] ? 1 : 0;
        }
        if (ferror(f)) {
            err("%s: %s", argv[i], strerror(errno));
            ret = 1;
        }
        fclose(f);
    }
    if (fflush(stdout) != 0)
        return 1;
    return found && !ret ? 0 : 1;
}
