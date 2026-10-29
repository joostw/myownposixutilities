/*
 * main.c - options, file names, and the order of the work.
 */
#include "yacc.h"

const char *file_prefix = "y", *sym_prefix;
int opt_d, opt_l, opt_t, opt_v;
char *code_name, *header_name, *verbose_name;

static void usage(void)
{
    fprintf(stderr, "usage: %s [-dltv] [-b file_prefix] [-p sym_prefix] grammar\n", progname);
    exit(2);
}

static char *concat(const char *a, const char *b)
{
    size_t n = strlen(a), m = strlen(b);
    char *s = xmalloc(n + m + 1);
    memcpy(s, a, n);
    memcpy(s + n, b, m + 1);
    return s;
}

static FILE *open_out(const char *name)
{
    FILE *fp = fopen(name, "w");
    if (!fp)
        fatal("cannot create %s: %s", name, strerror(errno));
    return fp;
}

static void close_out(FILE *fp, const char *name)
{
    if (fflush(fp) || ferror(fp) || fclose(fp))
        fatal("error writing %s: %s", name, strerror(errno));
}

void grammar_abort(void)
{
    if (opt_v && verbose_name) {
        FILE *fp = fopen(verbose_name, "w");
        if (fp) {
            write_verbose_error(fp);
            fclose(fp);
        }
    }
    exit(1);
}

int main(int argc, char **argv)
{
    int i;
    const char *grammar;
    FILE *in;

    setlocale(LC_ALL, "");
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || !a[1])
            break;
        if (!strcmp(a, "--")) {
            i++;
            break;
        }
        for (const char *c = a + 1; *c; c++) {
            switch (*c) {
            case 'd': opt_d = 1; break;
            case 'l': opt_l = 1; break;
            case 't': opt_t = 1; break;
            case 'v': opt_v = 1; break;
            case 'b':
            case 'p': {
                const char *arg;
                if (c[1])
                    arg = c + 1;
                else if (i + 1 < argc)
                    arg = argv[++i];
                else {
                    fprintf(stderr, "%s: option -%c requires an argument\n", progname, *c);
                    usage();
                    arg = NULL;
                }
                if (!*arg)
                    fatal("option -%c requires a non-empty argument", *c);
                if (*c == 'b')
                    file_prefix = arg;
                else
                    sym_prefix = arg;
                c = arg + strlen(arg) - 1;   /* the rest of the argument was consumed */
                break;
            }
            default:
                fprintf(stderr, "%s: unknown option -%c\n", progname, *c);
                usage();
            }
        }
    }
    if (argc - i != 1) {
        if (argc - i > 1)
            fprintf(stderr, "%s: only one grammar file may be given\n", progname);
        usage();
    }
    grammar = argv[i];
    if (strchr(file_prefix, '\n'))
        fatal("the file prefix contains a newline");
    if (sym_prefix && !is_c_name(sym_prefix))
        fatal("the symbol prefix %s is not a valid C identifier", sym_prefix);
    code_name = concat(file_prefix, ".tab.c");
    header_name = concat(file_prefix, ".tab.h");
    verbose_name = concat(file_prefix, ".output");

    input_name = grammar;
    in = fopen(grammar, "r");
    if (!in) {
        fprintf(stderr, "%s: cannot open %s: %s\n", progname, grammar, strerror(errno));
        nerrors++;
        grammar_abort();
    }
    read_grammar(in);
    fclose(in);
    if (nerrors)
        grammar_abort();

    build_lr0();
    build_lalr();
    build_actions();
    report_conflicts();

    {
        FILE *code = open_out(code_name);
        FILE *hdr = opt_d ? open_out(header_name) : NULL;
        write_code(code, hdr);
        close_out(code, code_name);
        if (hdr)
            close_out(hdr, header_name);
    }
    if (opt_v) {
        FILE *v = open_out(verbose_name);
        write_verbose(v);
        close_out(v, verbose_name);
    }
    return 0;
}
