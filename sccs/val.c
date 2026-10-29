/*
 * val.c - val: validate SCCS files (POSIX.1-2024, XCU val)
 *
 *   val -
 *   val [-s] [-m name] [-r SID] [-y type] file...
 *
 * The exit status is the OR of the bits of the standard: 0x80 no file,
 * 0x40 an unknown or repeated option, 0x20 a corrupted file, 0x10 a file
 * that cannot be opened or is not an SCCS file, 0x08 an invalid or
 * ambiguous SID, 0x04 a SID that does not exist, 0x02 the -y type and
 * 0x01 the -m name do not match.
 */

#include "sccs.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *line_in;     /* the line of the standard input, or NULL */
static bool silent;

static void report(const char *path, const char *msg)
{
    if (silent)
        return;
    if (line_in)
        printf("%s\n\n %s: %s\n", line_in, path, msg);
    else
        printf("%s: %s\n", path, msg);
}

/* one command line: the arguments in av[0..ac) (no program name) */
static int val_args(int ac, char **av)
{
    int code = 0;
    const char *m = NULL, *r = NULL, *y = NULL;
    bool sm = false, sr = false, sy = false, ss = false;
    silent = false;
    int i;
    for (i = 0; i < ac && av[i][0] == '-' && av[i][1]; i++) {
        if (strcmp(av[i], "--") == 0) {
            i++;
            break;
        }
        for (const char *p = av[i] + 1; *p; p++) {
            char c = *p;
            if (c == 's') {
                if (ss)
                    code |= 0x40;
                ss = silent = true;
                continue;
            }
            if (c != 'm' && c != 'r' && c != 'y') {
                code |= 0x40;
                report(av[i], "unknown option");
                continue;
            }
            const char *arg = p[1] ? p + 1 : (i + 1 < ac ? av[++i] : NULL);
            if (!arg) {
                code |= 0x40;
                report(av[i], "missing option-argument");
                break;
            }
            bool *seen = c == 'm' ? &sm : c == 'r' ? &sr : &sy;
            if (*seen)
                code |= 0x40;
            *seen = true;
            if (c == 'm')
                m = arg;
            else if (c == 'r')
                r = arg;
            else
                y = arg;
            break;
        }
    }
    if (code & 0x40) {
        if (!silent)
            report("val", "unknown or duplicate option");
    }
    if (i >= ac) {
        report("val", "missing file argument");
        return code | 0x80;
    }
    for (; i < ac; i++) {
        const char *path = av[i];
        if (access(path, R_OK) != 0 || strncmp(base_name(path), "s.", 2) != 0) {
            report(path, "cannot open file or file not SCCS");
            code |= 0x10;
            continue;
        }
        struct sfile sf;
        bool corrupt;
        /* the messages of the reader are val's report instead */
        int saved_status = status;
        quiet = true;
        bool ok = sfile_read(path, &sf, &corrupt);
        quiet = false;
        status = saved_status;
        if (!ok) {
            if (corrupt) {
                report(path, "corrupted SCCS file");
                code |= 0x20;
            } else {
                report(path, "cannot open file or file not SCCS");
                code |= 0x10;
            }
            sfile_free(&sf);
            continue;
        }
        if (r) {
            struct sid s;
            int n = sid_parse(r, &s);
            if (n == 0 || n == 1 || n == 3) {
                report(path, n ? "SID is ambiguous" : "SID is invalid");
                code |= 0x08;
            } else if (!by_sid(&sf, &s)) {
                report(path, "SID does not exist");
                code |= 0x04;
            }
        }
        if (y) {
            const char *t = flagval(&sf, 't');
            if (!t || strcmp(t, y) != 0) {
                report(path, "%Y%, -y mismatch");
                code |= 0x02;
            }
        }
        if (m && strcmp(module_name(&sf), m) != 0) {
            report(path, "%M%, -m mismatch");
            code |= 0x01;
        }
        sfile_free(&sf);
    }
    return code;
}

int val_main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "-") == 0) {
        int code = 0;
        char *line = NULL;
        size_t cap = 0;
        ssize_t n;
        while ((n = getline(&line, &cap, stdin)) > 0) {
            if (line[n - 1] == '\n')
                line[--n] = '\0';
            /* split on blanks, as is */
            char **av = NULL;
            int ac = 0;
            char *copy = xstrdup(line);
            for (char *p = copy; *p; ) {
                while (*p && isblank((unsigned char)*p))
                    *p++ = '\0';
                if (!*p)
                    break;
                av = xrealloc(av, (size_t)(ac + 1) * sizeof *av);
                av[ac++] = p;
                while (*p && !isblank((unsigned char)*p))
                    p++;
            }
            line_in = line;
            code |= val_args(ac, av);
            free(av);
            free(copy);
        }
        free(line);
        fflush(stdout);
        return code;
    }
    int code = val_args(argc - 1, argv + 1);
    fflush(stdout);
    return code;
}
