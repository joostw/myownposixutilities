/*
 * main.c - options, start-up, Send Mode and Receive Mode.
 */
#include "mailx.h"

void install_sigint(void);

static void usage(void)
{
    fprintf(stderr,
            "usage: mailx [-E] [-s subject] address...\n"
            "       mailx -e\n"
            "       mailx [-HiNn] [-F] [-u user]\n"
            "       mailx -f [-HiNn] [-F] [file]\n");
    exit(1);
}

static int has_messages(const char *path)
{
    FILE *fp = fopen(path, "r");
    char line[16];
    int r = 0;
    if (!fp)
        return 0;
    /* a non-empty mailbox: the first line starts a message */
    if (fgets(line, sizeof line, fp) && line[0])
        r = 1;
    fclose(fp);
    return r;
}

int main(int argc, char **argv)
{
    int c, opt_e = 0, opt_f = 0, opt_H = 0, opt_i = 0, opt_N = 0, opt_n = 0;
    const char *subject = NULL, *user = NULL;
    static const char *const imported[] = { "DEAD", "EDITOR", "MBOX", "LISTER", "PAGER", "SHELL", "VISUAL" };
    setlocale(LC_ALL, "");
    /* 1: defaults */
    var_set("asksub", "");
    var_set("header", "");
    var_set("save", "");
    var_set("prompt", "? ");
    var_set("toplines", "5");
    var_set("indentprefix", "\t");
    /* 2: options */
    while ((c = getopt(argc, argv, ":EeFfHiNns:u:")) != -1) {
        switch (c) {
        case 'E':
            opt_E = 1;
            break;
        case 'e':
            opt_e = 1;
            break;
        case 'F':
            opt_F = 1;
            break;
        case 'f':
            opt_f = 1;
            break;
        case 'H':
            opt_H = 1;
            break;
        case 'i':
            opt_i = 1;
            break;
        case 'N':
            opt_N = 1;
            break;
        case 'n':
            opt_n = 1;
            break;
        case 's':
            subject = optarg;
            break;
        case 'u':
            user = optarg;
            break;
        case ':':
            fprintf(stderr, "mailx: option requires an argument -- %c\n", optopt);
            usage();
            break;
        default:
            fprintf(stderr, "mailx: illegal option -- %c\n", optopt);
            usage();
        }
    }
    argc -= optind;
    argv += optind;
    if (opt_i)
        var_set("ignore", "");
    if (opt_N)
        var_unset("header");
    send_mode = argc > 0 && !opt_f;
    if (send_mode && (opt_e || user || opt_H)) {
        fprintf(stderr, "mailx: -e, -H and -u are not used when sending mail\n");
        usage();
    }
    if (!send_mode && subject && !opt_f) {
        fprintf(stderr, "mailx: -s requires an address\n");
        usage();
    }
    if (opt_f && argc > 1)
        usage();
    /* 3: the environment */
    for (size_t i = 0; i < sizeof imported / sizeof imported[0]; i++) {
        const char *v = getenv(imported[i]);
        if (v)
            var_set(imported[i], v);
    }
    /* 4, 5: start-up files */
    if (!opt_n)
        source_file("/etc/mailx.rc", 1);
    {
        const char *rc = getenv("MAILRC");
        if (rc)
            source_file(rc, 1);
        else {
            Buf p = { 0 };
            buf_fmt(&p, "%s/.mailrc", home_dir());
            source_file(p.s, 1);
            buf_free(&p);
        }
    }
    if (opt_e) {
        char *path = user ? system_mailbox(user) : system_mailbox(NULL);
        int r = has_messages(path) ? 0 : 1;
        free(path);
        return r;
    }
    if (send_mode) {
        Compose comp;
        int r;
        memset(&comp, 0, sizeof comp);
        for (int i = 0; i < argc; i++)
            split_addresses(argv[i], &comp.to);
        if (subject)
            comp.subject = xstrdup(subject);
        if (is_tty_in())
            install_sigint();
        r = collect(&comp, 0);
        if (r != 0) {
            compose_free(&comp);
            return 1;
        }
        r = send_message(&comp);
        compose_free(&comp);
        fflush(stdout);
        return r == 0 ? exit_status : 1;
    }
    /* Receive Mode */
    {
        char *path;
        int is_system = 0;
        if (opt_f) {
            if (argc == 1) {
                path = expand_filename(argv[0]);
                if (!path)
                    return 1;
            } else
                path = mbox_path();
        } else {
            path = user ? system_mailbox(user) : system_mailbox(NULL);
            is_system = 1;
        }
        if (mailbox_open(path, is_system, 0) < 0)
            return 1;
        if (mb.n == 0) {
            if (is_system) {
                printf("No mail for %s\n", user ? user : login_name());
                return 0;
            }
            if (opt_H)
                return 0;
        }
        if (opt_H) {
            for (int i = 1; i <= mb.n; i++)
                summary_line(stdout, i);
            return 0;
        }
        install_sigint();
        if (!var_isset("quiet"))
            printf("mailx version 1.0.  Type ? for help.\n");
        {
            int nnew = 0, nunread = 0;
            for (int i = 0; i < mb.n; i++) {
                if (mb.m[i].state == M_NEW)
                    nnew++;
                else if (mb.m[i].state == M_UNREAD)
                    nunread++;
            }
            printf("\"%s\": %d message%s", mb.path, mb.n, mb.n == 1 ? "" : "s");
            if (nnew)
                printf(" %d new", nnew);
            if (nunread)
                printf(" %d unread", nunread);
            if (mb.readonly)
                printf(" [Read only]");
            putchar('\n');
        }
        if (var_isset("header") && mb.n)
            headers_page(cur);
        free(path);
        command_loop();
    }
    return 0;
}
