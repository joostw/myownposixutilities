/*
 * main.c - command line handling for awk.
 *
 *   awk [-F sepstring] [-v assignment]... program [argument...]
 *   awk [-F sepstring] -f progfile [-f progfile]... [-v assignment]... [argument...]
 */
#include "awk.h"
#include <unistd.h>
#include <sys/resource.h>

extern char **environ;

static _Noreturn void usage(void)
{
    fprintf(stderr,
            "usage: %s [-F sepstring] [-v assignment]... program [argument...]\n"
            "       %s [-F sepstring] -f progfile [-f progfile]... [-v assignment]...\n"
            "              [argument...]\n", progname, progname);
    exit(2);
}

static void read_progfile(Buf *b, const char *name)
{
    FILE *fp;
    char tmp[8192];
    size_t n;
    if (strcmp(name, "-") == 0)
        fp = stdin;
    else if (!(fp = fopen(name, "r"))) {
        fprintf(stderr, "%s: can't open file %s: %s\n", progname, name, strerror(errno));
        exit(2);
    }
    while ((n = fread(tmp, 1, sizeof tmp, fp)) > 0)
        buf_add(b, tmp, n);
    if (ferror(fp)) {
        fprintf(stderr, "%s: error reading %s\n", progname, name);
        exit(2);
    }
    if (fp != stdin)
        fclose(fp);
    else
        clearerr(stdin);
    /* keep files lexically separate */
    if (b->len && b->p[b->len - 1] != '\n')
        buf_addc(b, '\n');
}

static void raise_stack_limit(void)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_STACK, &rl) == 0) {
        rlim_t want = (rlim_t)512 * 1024 * 1024;
        if (rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < want) {
            rl.rlim_cur = (rl.rlim_max == RLIM_INFINITY || rl.rlim_max > want) ? want : rl.rlim_max;
            setrlimit(RLIMIT_STACK, &rl);
        }
    }
}

int main(int argc, char **argv)
{
    Buf prog;
    int have_f = 0, i;
    char **vassign = xmalloc((size_t)argc * sizeof(char *));
    int nv = 0;
    const char *fs = NULL;
    int fs_order = -1;      /* position of -F among -v assignments */
    char **operands;
    int nops;

    setlocale(LC_ALL, "");
    {
        struct lconv *lc = localeconv();
        if (lc && lc->decimal_point && lc->decimal_point[0] && !lc->decimal_point[1])
            radix_char = (unsigned char)lc->decimal_point[0];
    }
    mb_mode = MB_CUR_MAX > 1;
    raise_stack_limit();

    buf_init(&prog);

    /* option parsing per XBD 12.2 Utility Syntax Guidelines */
    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        char *optarg;
        int c;
        if (a[0] != '-' || a[1] == 0)
            break;
        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        a++;
        while (*a) {
            c = *a++;
            if (c != 'F' && c != 'f' && c != 'v') {
                fprintf(stderr, "%s: unknown option -%c\n", progname, c);
                usage();
            }
            if (*a)
                optarg = a;
            else if (i + 1 < argc)
                optarg = argv[++i];
            else {
                fprintf(stderr, "%s: option requires an argument -- %c\n", progname, c);
                usage();
            }
            switch (c) {
            case 'F':
                fs = optarg;
                fs_order = nv;
                break;
            case 'f':
                read_progfile(&prog, optarg);
                have_f = 1;
                break;
            case 'v':
                if (!is_assignment_operand(optarg)) {
                    fprintf(stderr, "%s: invalid -v argument: %s\n", progname, optarg);
                    exit(2);
                }
                vassign[nv++] = optarg;
                break;
            }
            break;
        }
    }

    if (!have_f) {
        if (i >= argc) {
            fprintf(stderr, "%s: no program given\n", progname);
            usage();
        }
        buf_add(&prog, argv[i], strlen(argv[i]));
        buf_addc(&prog, '\n');
        i++;
    }

    /* ARGV[0] is the command name, then the remaining operands */
    nops = argc - i + 1;
    operands = xmalloc((size_t)(nops + 1) * sizeof(char *));
    operands[0] = argv[0];
    {
        int k;
        for (k = 1; k < nops; k++)
            operands[k] = argv[i + k - 1];
        operands[nops] = NULL;
    }

    run_init(nops, operands, environ);
    if (!prog.p)
        buf_add(&prog, "", 0);
    parse_program(prog.p, prog.len);

    /* -v assignments, with -F processed in command line order */
    for (i = 0; i <= nv; i++) {
        if (fs && fs_order == i) {
            char *tmp;
            size_t fl = strlen(fs);
            tmp = xmalloc(fl + 4);
            memcpy(tmp, "FS=", 3);
            memcpy(tmp + 3, fs, fl + 1);
            cmdline_assign(tmp);
            free(tmp);
        }
        if (i < nv)
            cmdline_assign(vassign[i]);
    }

    return run_program();
}
