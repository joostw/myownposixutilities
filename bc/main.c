/*
 * main.c - options, diagnostics, interrupts and the order of the work.
 */
#include "bc.h"
#include <pthread.h>

int interactive;
int had_error;
const char *cur_file = "";
int cur_line = 1;

void bc_nomem(void)
{
    fputs("bc: out of memory\n", stderr);
    exit(2);
}

static void diag(const char *kind, const char *fmt, va_list ap)
{
    fflush(stdout);
    fprintf(stderr, "bc: %s:%d: ", cur_file, cur_line);
    if (kind)
        fprintf(stderr, "%s: ", kind);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void bc_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    diag(NULL, fmt, ap);
    va_end(ap);
    had_error = 1;
}

void bc_runtime_error(const char *fmt, ...)
{
    va_list ap;
    int save = cur_line;
    va_start(ap, fmt);
    if (exec_line)
        cur_line = exec_line;
    diag("runtime error", fmt, ap);
    cur_line = save;
    va_end(ap);
    had_error = 1;
    exec_abort();
}

void bc_warning(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    diag("warning", fmt, ap);
    va_end(ap);
}

void bc_quit(int status)
{
    fflush(stdout);
    exit(status ? status : had_error);
}

static void on_interrupt(int sig)
{
    (void)sig;
    interrupted = 1;
}

static size_t thread_stack;

static void *parse_thread(void *arg)
{
    char base;
    (void)arg;
    set_stack_limit(&base, thread_stack);
    yyparse();
    return NULL;
}

/*
 * Function calls recurse in the interpreter: the parser runs in a thread
 * with a large stack (reserved, not used, memory) so that deep bc
 * recursion fails with a message rather than a crash.
 */
static void run_with_big_stack(void)
{
    pthread_attr_t attr;
    pthread_t t;
    size_t sizes[] = { (size_t)1 << 30, (size_t)512 << 20, (size_t)256 << 20, (size_t)64 << 20,
                       (size_t)16 << 20 };
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        if (pthread_attr_init(&attr))
            break;
        thread_stack = sizes[i];
        if (!pthread_attr_setstacksize(&attr, sizes[i]) && !pthread_create(&t, &attr, parse_thread, NULL)) {
            pthread_attr_destroy(&attr);
            pthread_join(t, NULL);
            return;
        }
        pthread_attr_destroy(&attr);
    }
    /* no thread: the ordinary stack, assumed to be at least 8 MB */
    {
        char base;
        set_stack_limit(&base, (size_t)8 << 20);
        yyparse();
    }
}

static void usage(void)
{
    fputs("usage: bc [-l] [file...]\n", stderr);
    exit(2);
}

int main(int argc, char **argv)
{
    int i, opt_l = 0;
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
            if (*c == 'l')
                opt_l = 1;
            else {
                fprintf(stderr, "bc: unknown option -%c\n", *c);
                usage();
            }
        }
    }
    for (; i < argc; i++)
        scanner_add_file(argv[i]);
    interactive = isatty(0) && isatty(1);
    if (interactive) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_interrupt;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGINT, &sa, NULL);
    }
    exec_init();
    if (opt_l) {
        math_define();
        reg_scale = 20;
    }
    scanner_next_input();
    run_with_big_stack();
    bc_quit(0);
    return 0;
}
