/*
 * logger.c - logger: log messages (POSIX.1-2024, XCU logger)
 *
 * Synopsis: logger [-i] [-f file] [-p priority] [-t tag] [string...]
 *
 * The logging facility is the system log daemon (syslogd): each message
 * is sent to its socket, /dev/log, as syslog() would send it,
 * "<PRI>Mmm dd hh:mm:ss TAG[PID]: MESSAGE" (RFC 3164), in a datagram, or
 * on a stream ended by a NUL when the socket is a stream socket.  logger
 * does this itself, not with syslog(), so that it can report a message it
 * could not send.
 *
 * Choices where the standard leaves room:
 *  - The default tag is the login name (getlogin(), else the name of the
 *    real user ID, else "logger").
 *  - Lines read from the standard input or the file are logged one message
 *    each; empty lines are not logged.  The string operands are one
 *    message.
 *  - A priority is facility.level, or a level alone (the facility user).
 *    The facilities: user and local0 to local7, and also kern, mail, news,
 *    uucp, daemon, auth, cron and lpr (the others of <syslog.h>); the
 *    levels: emerg, alert, crit, err, warning, notice, info, debug (also
 *    error, warn and panic, the historical names).  Case does not matter.
 *  - A message that cannot be sent (no log daemon) is an error: a
 *    diagnostic, and status 1 at the end.  So is an unknown priority.
 *  - -u socket sends to that socket instead of /dev/log (an extension, as
 *    in util-linux logger).
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <locale.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

static const char *sockpath = "/dev/log";
static const char *tag;
static bool opt_i;
static int pri = LOG_USER | LOG_NOTICE;
static int sock = -1, socktype;
static int status;

static const struct {
    const char *name;
    int value;
} facilities[] = {
    { "user", LOG_USER }, { "local0", LOG_LOCAL0 }, { "local1", LOG_LOCAL1 },
    { "local2", LOG_LOCAL2 }, { "local3", LOG_LOCAL3 }, { "local4", LOG_LOCAL4 },
    { "local5", LOG_LOCAL5 }, { "local6", LOG_LOCAL6 }, { "local7", LOG_LOCAL7 },
    { "kern", LOG_KERN }, { "mail", LOG_MAIL }, { "news", LOG_NEWS }, { "uucp", LOG_UUCP },
    { "daemon", LOG_DAEMON }, { "auth", LOG_AUTH }, { "cron", LOG_CRON }, { "lpr", LOG_LPR },
}, levels[] = {
    { "emerg", LOG_EMERG }, { "alert", LOG_ALERT }, { "crit", LOG_CRIT }, { "err", LOG_ERR },
    { "warning", LOG_WARNING }, { "notice", LOG_NOTICE }, { "info", LOG_INFO },
    { "debug", LOG_DEBUG }, { "error", LOG_ERR }, { "warn", LOG_WARNING }, { "panic", LOG_EMERG },
};

static int lookup(const char *s, size_t n, bool fac)
{
    size_t count = fac ? sizeof facilities / sizeof facilities[0] : sizeof levels / sizeof levels[0];
    for (size_t i = 0; i < count; i++) {
        const char *name = fac ? facilities[i].name : levels[i].name;
        if (strlen(name) == n && strncasecmp(name, s, n) == 0)
            return fac ? facilities[i].value : levels[i].value;
    }
    return -1;
}

static bool parse_priority(const char *s)
{
    const char *dot = strchr(s, '.');
    int f = LOG_USER, l;
    if (dot) {
        if ((f = lookup(s, (size_t)(dot - s), true)) < 0)
            return false;
        l = lookup(dot + 1, strlen(dot + 1), false);
    } else
        l = lookup(s, strlen(s), false);
    if (l < 0)
        return false;
    pri = f | l;
    return true;
}

/* connect to the log socket: a datagram socket, else a stream one */
static bool open_log(void)
{
    if (sock >= 0)
        return true;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    if (strlen(sockpath) >= sizeof sa.sun_path) {
        errno = ENAMETOOLONG;
        return false;
    }
    strcpy(sa.sun_path, sockpath);
    int types[2] = { SOCK_DGRAM, SOCK_STREAM };
    int err = 0;
    for (int i = 0; i < 2; i++) {
        int fd = socket(AF_UNIX, types[i], 0);
        if (fd < 0) {
            err = errno;
            continue;
        }
        if (connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0) {
            sock = fd;
            socktype = types[i];
            return true;
        }
        err = errno;
        close(fd);
        if (err != EPROTOTYPE)
            break;
    }
    errno = err;
    return false;
}

static void log_message(const char *msg, size_t len)
{
    char head[512];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char stamp[32] = "";
    if (tm) {
        /* the month in English, as syslog() writes it */
        static const char mon[][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        snprintf(stamp, sizeof stamp, "%s %2d %02d:%02d:%02d", mon[tm->tm_mon], tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec);
    }
    int hl;
    if (opt_i)
        hl = snprintf(head, sizeof head, "<%d>%s %s[%ld]: ", pri, stamp, tag, (long)getpid());
    else
        hl = snprintf(head, sizeof head, "<%d>%s %s: ", pri, stamp, tag);
    if (hl < 0 || (size_t)hl >= sizeof head)
        hl = (int)strlen(head);
    size_t n = (size_t)hl + len + 1;
    char *buf = malloc(n);
    if (!buf) {
        fprintf(stderr, "logger: out of memory\n");
        exit(1);
    }
    memcpy(buf, head, (size_t)hl);
    memcpy(buf + hl, msg, len);
    buf[n - 1] = '\0';
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!open_log())
            break;
        size_t out = socktype == SOCK_STREAM ? n : n - 1;   /* a stream: with the NUL */
        ssize_t w = send(sock, buf, out, 0);
        if (w == (ssize_t)out) {
            free(buf);
            return;
        }
        /* the daemon may have been restarted: once more, on a new socket */
        int e = errno;
        close(sock);
        sock = -1;
        errno = e;
    }
    fprintf(stderr, "logger: cannot log to %s: %s\n", sockpath, strerror(errno));
    status = 1;
    free(buf);
}

static void log_stream(FILE *fp, const char *name)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, fp)) >= 0) {
        if (n > 0 && line[n - 1] == '\n')
            n--;
        if (n > 0)
            log_message(line, (size_t)n);
    }
    if (ferror(fp)) {
        fprintf(stderr, "logger: %s: %s\n", name, strerror(errno));
        status = 1;
    }
    free(line);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    const char *file = NULL;
    int c;
    while ((c = getopt(argc, argv, "if:p:t:u:")) != -1) {
        switch (c) {
        case 'i': opt_i = true; break;
        case 'f': file = optarg; break;
        case 'p':
            if (!parse_priority(optarg)) {
                fprintf(stderr, "logger: -p %s: not a priority (facility.level)\n", optarg);
                return 1;
            }
            break;
        case 't': tag = optarg; break;
        case 'u': sockpath = optarg; break;
        default:
            fprintf(stderr, "usage: logger [-i] [-f file] [-p priority] [-t tag] [string...]\n");
            return 2;
        }
    }
    if (!tag) {
        tag = getlogin();
        if (!tag) {
            struct passwd *pw = getpwuid(getuid());
            tag = pw ? pw->pw_name : "logger";
        }
    }
    if (optind < argc) {
        size_t len = 0;
        for (int i = optind; i < argc; i++)
            len += strlen(argv[i]) + 1;
        char *msg = malloc(len);
        if (!msg) {
            fprintf(stderr, "logger: out of memory\n");
            return 1;
        }
        char *p = msg;
        for (int i = optind; i < argc; i++) {
            if (i > optind)
                *p++ = ' ';
            size_t l = strlen(argv[i]);
            memcpy(p, argv[i], l);
            p += l;
        }
        *p = '\0';
        log_message(msg, (size_t)(p - msg));
        free(msg);
    } else if (file) {
        FILE *fp = fopen(file, "r");
        if (!fp) {
            fprintf(stderr, "logger: %s: %s\n", file, strerror(errno));
            return 1;
        }
        log_stream(fp, file);
        fclose(fp);
    } else
        log_stream(stdin, "standard input");
    return status;
}
