/*
 * arith.h - values shared by the arithmetic grammar (arith.y) and its
 * scanner (arith.l).
 */
#ifndef ARITH_H
#define ARITH_H

typedef struct AVal {
    long v;
    char *name;         /* an lvalue: the variable's name, or NULL */
} AVal;

typedef struct ATern { long cv, tv; } ATern;            /* c ? t : */
typedef struct AHead { char *name; const char *op; } AHead;    /* name op= */
typedef struct ANum { char *text; int bad; } ANum;      /* a constant as written */

/* arith.l */
void arith_scan_begin(const char *expr);
void arith_scan_end(void);
extern int arith_last_token;    /* the last token read: 0 at the end */

#endif
