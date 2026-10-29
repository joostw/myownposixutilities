/*
 * sccs.h - what the SCCS utilities share (sfile.c, ldiff.c and the
 * utilities): the SCCS file, SIDs, retrieval, the p-file and locks
 */

#ifndef SCCS_H
#define SCCS_H

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

#define SOH '\001'

extern const char *progname;    /* for messages */
extern int status;              /* the exit status so far */
extern bool quiet;              /* no messages from warn() and err() */

/* ---- memory and strings ---- */

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *xasprintf(const char *fmt, ...);   /* a formatted, malloc'd string */
void warn(const char *fmt, ...);        /* "prog: message", no status */
void err(const char *fmt, ...);         /* the same, status 1 */

struct strs {                   /* a growable list of strings */
    char **v;
    int n, cap;
};
void strs_add(struct strs *l, const char *s);
void strs_free(struct strs *l);

/* ---- SIDs ---- */

struct sid {
    int r, l, b, s;             /* 0 when not there */
};

/* parse "R[.L[.B[.S]]]": the number of components, or 0 if not valid */
int sid_parse(const char *s, struct sid *sid);
char *sid_str(const struct sid *sid, char *buf);   /* buf: 64 bytes */
bool sid_eq(const struct sid *a, const struct sid *b);
bool sid_branch(const struct sid *s);              /* has B.S */

/* ---- the SCCS file ---- */

struct delta {
    char type;                  /* 'D', or 'R' (removed) */
    struct sid sid;
    char date[9], time[9];      /* "yy/mm/dd", "hh:mm:ss" */
    char *user;
    int serial, pred;
    long ins, del, unch;
    int *inc, ninc, *exc, nexc, *ign, nign;
    struct strs mrs, comments;
};

struct sfile {
    char *path;                 /* as named */
    struct delta *d;            /* in the order of the file: newest first */
    int nd;
    int maxserial;
    struct strs users;
    char *flag[26];             /* the value ("" for none), or NULL */
    struct strs desc;           /* descriptive text */
    struct strs body;           /* the weave, without newlines */
    unsigned sum_stored, sum_computed;
};

/* read path into sf; false, with a message, if it cannot be read or is
   not a valid SCCS file (*corrupt set for the latter) */
bool sfile_read(const char *path, struct sfile *sf, bool *corrupt);
void sfile_free(struct sfile *sf);
/* do not check the checksum in sfile_read (admin -h and -z) */
void sccs_ignore_sum(bool on);
/* write sf as the x-file and rename it over the SCCS file */
bool sfile_write(struct sfile *sf, mode_t mode);

struct delta *by_serial(const struct sfile *sf, int serial);
struct delta *by_sid(const struct sfile *sf, const struct sid *s);   /* D only */
const char *flagval(const struct sfile *sf, char c);                 /* or NULL */
const char *module_name(const struct sfile *sf);                     /* %M% */

/* the names of the files next to path: tag 'p', 'z', 'x', 'q', 'l', or 'g'
   (the g-file, in the current directory) */
char *aux_name(const char *path, char tag);
const char *base_name(const char *path);

/* ---- choosing deltas ---- */

/* the delta for a get, by the table of the standard; false with a message */
bool choose(const struct sfile *sf, const char *rsid, bool use_t, struct delta **got);
/* the SID of the new delta for get -e from got; reserved: the new SIDs in
   the p-file */
bool new_sid(const struct sfile *sf, const char *rsid, const struct delta *got,
             bool want_branch, const struct strs *reserved, struct sid *out);

enum { AP_NONE = 0, AP_APPLIED = 1, AP_INCLUDED = 2, AP_EXCLUDED = 4, AP_IGNORED = 8,
       AP_CUTOFF = 16, AP_PATH = 32 };

/* the state of each serial (index 1..maxserial) for retrieving target with
   the -i and -x lists and the cutoff (0: none); false with a message */
bool applied_set(const struct sfile *sf, const struct delta *target, const char *ilist,
                 const char *xlist, time_t cutoff, int **state);
/* the serials of a list of SIDs ("1.2,1.4-1.6"); false with a message */
bool sid_list(const struct sfile *sf, const char *list, int **serials, int *n);

/* the retrieved text: lines, and for each the serial that inserted it */
struct text {
    struct strs lines;
    int *by;
};
void retrieve(const struct sfile *sf, const int *state, struct text *t);
void text_free(struct text *t);

/* ---- dates ---- */

void now_strings(char *date, char *tim);      /* yy/mm/dd, hh:mm:ss */
time_t delta_time(const struct delta *d);
bool parse_cutoff(const char *s, time_t *t);

/* ---- users ---- */

const char *login_name(void);   /* of the real user */
bool may_edit(const struct sfile *sf, const char *login);

/* ---- the p-file ---- */

struct pentry {
    char *got, *newsid, *user, *date, *time, *ilist, *xlist;
};
struct pfile {
    struct pentry *e;
    int n;
};
bool pfile_read(const char *path, struct pfile *p);  /* none is empty */
bool pfile_write(const char *path, const struct pfile *p);
void pfile_free(struct pfile *p);

/* ---- the lock (z-file) ---- */

bool lock_sccs(const char *path);
void unlock_sccs(void);

/* ---- operands: files, directories, "-" ---- */

/* call fn for each SCCS file named by the operands; *multi set when the
   names are to be written before the output of each */
void for_each_file(int argc, char **argv, void (*fn)(const char *), bool *multi);

/* ---- comments and MRs ---- */

/* read lines with \ continuations from the standard input after a prompt
   (when it is a terminal): the lines; false at end of input */
bool read_continued(const char *prompt, struct strs *out);
/* split a list of MRs on blanks and newlines */
void split_mrs(const char *s, struct strs *out);
/* run the v-flag program on the MRs: true if they are valid */
bool check_mrs(const struct sfile *sf, const struct strs *mrs);

/* ---- id keywords ---- */

bool has_keywords(const char *line);

struct kwctx {
    const struct sfile *sf;
    const struct delta *got;
    const struct delta *newest;     /* the newest delta applied */
    char today[9], now[9];
    char abspath[8200];
};
void kw_init(struct kwctx *k, const struct sfile *sf, const struct delta *got, const int *state);
/* line with its keywords replaced (malloc'd); *found set if there was one */
char *kw_expand(const struct kwctx *k, const char *line, long lineno, bool *found);

/* ---- line differences (ldiff.c) ---- */

/* the edit script from a[0..na) to b[0..nb): for each line of a, whether
   it is kept; for each line of b, whether it is new */
void ldiff(char **a, int na, char **b, int nb, bool *keep_a, bool *new_b);

/* ---- options ---- */

/* options as getopt() reads them, and also options with an optional
   argument that is only taken when attached ("y;" in spec): sopt returns
   the option, or -1 at the first operand or after "--", or '?' with a
   message for an unknown option or a missing argument */
struct opts {
    int ind;                    /* the next argument; 1 to start */
    int pos;                    /* in it */
    char *arg;                  /* the option-argument, or NULL */
};
int sopt(int argc, char **argv, const char *spec, struct opts *o);

/* ---- the utilities ---- */

int admin_main(int argc, char **argv);
int delta_main(int argc, char **argv);
int get_main(int argc, char **argv);
int prs_main(int argc, char **argv);
int rmdel_main(int argc, char **argv);
int sact_main(int argc, char **argv);
int unget_main(int argc, char **argv);
int val_main(int argc, char **argv);
int what_main(int argc, char **argv);
int sccs_main(int argc, char **argv);

#endif
