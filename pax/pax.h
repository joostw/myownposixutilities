/*
 * pax.h - declarations for pax (POSIX.1-2024, XCU pax).
 */
#ifndef PAX_H
#define PAX_H

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <grp.h>
#include <inttypes.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <regex.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ---------- util.c ---------- */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
void warnx_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void errx_(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
void warn_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));  /* with strerror(errno) */
extern int exit_status;
extern const char *progname;

typedef struct Buf {
    char *s;
    size_t len, cap;
} Buf;
void buf_add(Buf *b, const void *p, size_t n);
void buf_adds(Buf *b, const char *s);
void buf_addc(Buf *b, int c);
void buf_fmt(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void buf_free(Buf *b);

/* keyword/value list (pax extended header records) */
typedef struct Kv {
    char *key;
    char *val;
    size_t vlen;
    struct Kv *next;
} Kv;
void kv_set(Kv **list, const char *key, const char *val, size_t vlen);
Kv *kv_get(Kv *list, const char *key);
void kv_free(Kv **list);
Kv *kv_copy(Kv *list);

/* ---------- entries ---------- */
enum { T_REG, T_DIR, T_LNK, T_HARD, T_CHR, T_BLK, T_FIFO, T_SOCK, T_CONTIG, T_OTHER };
enum { F_NONE, F_USTAR, F_PAX, F_CPIO, F_NEWC, F_OLDTAR };

typedef struct Entry {
    char *path;            /* member pathname */
    char *link;            /* hard link target or symbolic link contents */
    int type;
    mode_t perm;           /* 07777 bits */
    uid_t uid;
    gid_t gid;
    char *uname, *gname;
    off_t size;            /* bytes of data in the archive */
    struct timespec mtime, atime;
    int has_atime;
    unsigned long devmajor, devminor;
    unsigned long nlink;
    unsigned long cdev, cino;   /* cpio identity */
    int typeflag;          /* tar typeflag as read */
    int fmt;               /* format it was read from */
    unsigned char hdr[512];     /* raw header (tar: 512, cpio: up to 110) */
    int hdrlen;
    Kv *xrec;              /* extended header records applied to this entry */
    int invalid;           /* a name from the records is invalid here: 1 untranslatable, 2 too long */
    char *raw_path, *raw_link;  /* those names as the records hold them */
    /* write side */
    char *src;             /* file in the file system */
    struct stat st;
    int is_operand;
} Entry;
void entry_free(Entry *e);
void entry_init(Entry *e);

/* ---------- archive.c ---------- */
extern int arc_fd;
extern long blocksize;
extern int arc_fmt;           /* format being read or written */
void arc_open_read(const char *name);
void arc_open_write(const char *name, int append);
int arc_read(void *p, size_t n);          /* exactly n bytes, 0 at EOF before any byte */
void arc_read_full(void *p, size_t n);    /* error at EOF */
void arc_skip(off_t n);
void arc_write(const void *p, size_t n);
void arc_pad_write(size_t n);             /* n zero bytes */
void arc_finish_write(void);
void arc_unread(const void *p, size_t n);
off_t arc_offset(void);                    /* logical offset of reading */
void arc_seek_write(off_t off);
int detect_format(void);
extern dev_t arc_dev;
extern ino_t arc_ino;
extern int arc_is_file;

/* ---------- tar.c, cpio.c ---------- */
int tar_read_header(Entry *e);            /* 1 entry, 0 end */
void tar_write_entry(Entry *e);
void tar_write_end(void);
int cpio_read_header(Entry *e);
void cpio_write_entry(Entry *e);
void cpio_write_end(void);
extern Kv *opt_global, *opt_local;        /* -o keyword=value and keyword:=value */
extern char **opt_delete;
extern int nopt_delete;
extern char *opt_exthdr_name, *opt_globexthdr_name;
extern int opt_linkdata, opt_times;
extern char *opt_invalid;
void write_global_header(void);
int keyword_deleted(const char *key);
extern off_t tar_end_off, cpio_end_off;

/* data copying between archive and files */
int copy_file_to_archive(Entry *e);
void read_entry_data(Entry *e, int fd);   /* fd < 0: skip */

/* ---------- charset.c ---------- */
int to_utf8(const char *s, char **out);   /* 0 if it cannot be translated */
void decode_entry_names(Entry *e, Kv *m);
int invalid_action(Entry *e);             /* read mode: 0 to skip the entry */
int invalid_rename(const Entry *e);       /* read mode: ask for a new name */

/* ---------- options ---------- */
extern int opt_r, opt_w, opt_a, opt_c, opt_d, opt_i, opt_k, opt_l, opt_n, opt_t, opt_u, opt_v, opt_X;
extern int opt_H, opt_L;
extern int opt_fmt;
extern char *opt_listopt;
extern int keep_atime, keep_mtime, keep_owner, keep_mode;  /* -p */
extern int is_root;

/* ---------- names.c ---------- */
extern char **patterns;
extern int npatterns;
int select_member(Entry *e);
void report_unmatched(void);
void add_subst(const char *arg);
int nsubsts(void);
int apply_substs(char **name, int is_symlink_contents);   /* returns 0 if the name became empty */
int interactive_rename(char **name);       /* 0: skip */
char *map_link_name(const char *orig);
void note_renamed(const char *orig, const char *now);
int newer_than_existing(Entry *e, const char *path);
int all_patterns_done(void);

/* ---------- extract.c ---------- */
int extract_entry(Entry *e, int src_fd, const char *dest);
void finish_directories(void);

/* ---------- list.c ---------- */
void list_entry(Entry *e);
void ls_mode_string(Entry *e, char *buf);

/* ---------- walk.c ---------- */
typedef void (*WalkFn)(Entry *e);
void walk_operand(const char *path, WalkFn fn);
extern int walk_prune;   /* set by a WalkFn: do not descend */

#endif
