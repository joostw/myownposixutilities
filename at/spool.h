/*
 * spool.h - the at spool: where jobs are kept, and what a job file holds
 * (shared by at, batch and atd)
 */

#ifndef SPOOL_H
#define SPOOL_H

#include <stdbool.h>
#include <sys/types.h>
#include <time.h>

/* the directories (make can set them: -DAT_SPOOL=... -DAT_CONFDIR=...) */
#ifndef AT_SPOOL
#define AT_SPOOL "/var/spool/posixat"
#endif
#ifndef AT_CONFDIR
#define AT_CONFDIR "/etc"
#endif

/* the spool directory and the directory of at.allow and at.deny: the
   environment variables AT_SPOOL and AT_CONFDIR when the program is not
   set-user-ID or set-group-ID (so tests and private installations can use
   their own), else the compiled-in ones */
const char *spool_dir(void);
const char *conf_dir(void);

/* a job: the file <spool>/<id>, id "<number>.<queue letter>"; the first
   lines are comments with the fields, up to "# end" */
struct job {
    char id[64];
    uid_t owner;
    time_t when;
    char queue;
    bool mail;
    char shell[1024];
};

/* read the fields of the job file path; false if it is not a job file */
bool job_read(const char *path, struct job *j);

/* is the job id well formed? */
bool job_id_ok(const char *id);

/* the time as at writes it: "%a %b %e %T %Y" (the POSIX locale's names) */
void job_date(char *buf, size_t size, time_t t);

/* is the user allowed by <conf>/<base>.allow and <base>.deny? */
bool user_allowed(const char *base, const char *user, bool privileged);

#endif
