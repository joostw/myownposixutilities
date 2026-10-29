/*
 * cron.h - the crontab spool, and the entries of a crontab (shared by
 * crontab and crond)
 */

#ifndef CRON_H
#define CRON_H

#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#ifndef CRON_SPOOL
#define CRON_SPOOL "/var/spool/posixcron"
#endif
#ifndef CRON_CONFDIR
#define CRON_CONFDIR "/etc"
#endif

/* the spool directory (a file per user, named by the login name) and the
   directory of cron.allow and cron.deny: CRON_SPOOL and CRON_CONFDIR of
   the environment when the program is not set-user-ID or set-group-ID */
const char *cron_spool(void);
const char *cron_confdir(void);

/* an entry: the five patterns as bit sets, and the command */
struct entry {
    unsigned long long minute;  /* bits 0-59 */
    unsigned long hour;         /* 0-23 */
    unsigned long mday;         /* 1-31 */
    unsigned long month;        /* 1-12 */
    unsigned long wday;         /* 0-6 */
    bool mday_star, month_star, wday_star;
    char *command;              /* the sixth field, as written */
};

/* parse a line: 1 an entry, 0 a blank or comment line, -1 an error (a
   message in err) */
int cron_parse(const char *line, struct entry *e, char *err, size_t errsize);

/* does the entry's time match tm? */
bool cron_match(const struct entry *e, const struct tm *tm);

/* the command field: the command (up to the first '%' not after a
   backslash) and the standard input (the rest, each such '%' a newline,
   and a newline at the end); "\%" is a '%', other backslashes are kept
   (for the shell); malloc()ed */
void cron_command(const char *field, char **cmd, char **input);

/* is the user allowed by cron.allow and cron.deny? */
bool cron_allowed(const char *user, bool privileged);

#endif
