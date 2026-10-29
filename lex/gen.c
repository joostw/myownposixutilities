/*
 * gen.c - writing lex.yy.c: declarations and macros, the copied code,
 * the tables of the automaton, the buffer and input routines, yylex()
 * with the actions, and the user subroutines.
 */
#include "lex.h"

const char *out_name = "lex.yy.c";
long table_entries;

typedef struct Out {
    FILE *fp;
    int line;
} Out;

static void put(Out *o, const char *s)
{
    for (const char *q = s; *q; q++)
        if (*q == '\n')
            o->line++;
    fputs(s, o->fp);
}

static void putf(Out *o, const char *fmt, ...)
{
    Str s = { 0 };
    char tmp[512];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n >= 0 && (size_t)n < sizeof tmp) {
        put(o, tmp);
        return;
    }
    s.s = xmalloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(s.s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    put(o, s.s);
    free(s.s);
}

static void put_quoted(Out *o, const char *s)
{
    put(o, "\"");
    for (; *s; s++) {
        char b[8];
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
            snprintf(b, sizeof b, "\\%c", c);
        else if (c < 0x20 || c >= 0x7f)
            snprintf(b, sizeof b, "\\%03o", c);
        else
            snprintf(b, sizeof b, "%c", c);
        put(o, b);
    }
    put(o, "\"");
}

static void line_to(Out *o, int line)
{
    const char *f;
    int l;
    where(line, &f, &l);
    putf(o, "#line %d ", l);
    put_quoted(o, f);
    put(o, "\n");
}

static void line_back(Out *o)
{
    putf(o, "#line %d ", o->line + 1);
    put_quoted(o, out_name);
    put(o, "\n");
}

static void put_code(Out *o, const Code *c)
{
    size_t n = strlen(c->text);
    line_to(o, c->line);
    put(o, c->text);
    if (n == 0 || c->text[n - 1] != '\n')
        put(o, "\n");
    line_back(o);
}

static const char *int_type(const int *v, int n)
{
    int lo = 0, hi = 0;
    for (int i = 0; i < n; i++) {
        if (v[i] < lo)
            lo = v[i];
        if (v[i] > hi)
            hi = v[i];
    }
    if (lo >= -127 && hi <= 127)
        return "signed char";
    if (lo >= -32767 && hi <= 32767)
        return "short";
    return "int";
}

static void put_array(Out *o, const char *name, const int *v, int n)
{
    putf(o, "static const %s %s[%d] = {", int_type(v, n), name, n ? n : 1);
    for (int i = 0; i < n; i++) {
        if (i % 12 == 0)
            put(o, "\n   ");
        putf(o, " %d%s", v[i], i + 1 < n ? "," : "");
    }
    if (!n)
        put(o, " 0");
    put(o, "\n};\n");
}

/* ---------- the fixed parts of the scanner ---------- */

static const char *const skel_runtime =
    "static char *yy_buf;            /* the input read so far */\n"
    "static long yy_cap, yy_pos, yy_end;\n"
    "static int yy_eof;\n"
    "static long yy_hold_pos = -1;   /* where yytext's NUL replaced a character */\n"
    "static char yy_hold_char;\n"
    "static long yy_tok;             /* the start of yytext in the buffer */\n"
    "static int yy_lastc = '\\n';     /* the last character consumed */\n"
    "static int yy_init_done;\n"
    "static int *yy_hist;            /* the states of the current scan */\n"
    "static long yy_histcap;\n"
    "\n"
    "static void yy_fatal(const char *msg)\n"
    "{\n"
    "    (void)fprintf(stderr, \"%s\\n\", msg);\n"
    "    exit(2);\n"
    "}\n"
    "\n"
    "static void yy_init(void)\n"
    "{\n"
    "    yy_init_done = 1;\n"
    "    if (!yyin)\n"
    "        yyin = stdin;\n"
    "    if (!yyout)\n"
    "        yyout = stdout;\n"
    "    if (!yy_buf) {\n"
    "        yy_cap = 16384;\n"
    "        yy_buf = (char *)malloc((size_t)yy_cap);\n"
    "        yy_histcap = 1024;\n"
    "        yy_hist = (int *)malloc((size_t)yy_histcap * sizeof *yy_hist);\n"
    "        if (!yy_buf || !yy_hist)\n"
    "            yy_fatal(\"lex scanner: out of memory\");\n"
    "    }\n"
    "}\n"
    "\n"
    "static void yy_set_text(void)\n"
    "{\n"
    "#ifdef YY_TEXT_ARRAY\n"
    "    if (yyleng >= YYLMAX)\n"
    "        yy_fatal(\"lex scanner: token too long for yytext (YYLMAX)\");\n"
    "    memcpy(yytext, yy_buf + yy_tok, (size_t)yyleng);\n"
    "    yytext[yyleng] = 0;\n"
    "#else\n"
    "    yytext = yy_buf + yy_tok;\n"
    "    yy_hold_pos = yy_tok + yyleng;\n"
    "    yy_hold_char = yy_buf[yy_hold_pos];\n"
    "    yy_buf[yy_hold_pos] = 0;\n"
    "#endif\n"
    "}\n"
    "\n"
    "static void yy_restore(void)\n"
    "{\n"
    "    if (yy_hold_pos >= 0) {\n"
    "        yy_buf[yy_hold_pos] = yy_hold_char;\n"
    "        yy_hold_pos = -1;\n"
    "    }\n"
    "}\n"
    "\n"
    "static void yy_grow(long need)\n"
    "{\n"
    "    char *nb;\n"
    "    long n = yy_cap;\n"
    "    while (n < need)\n"
    "        n *= 2;\n"
    "    nb = (char *)realloc(yy_buf, (size_t)n);\n"
    "    if (!nb)\n"
    "        yy_fatal(\"lex scanner: out of memory\");\n"
    "    yy_buf = nb;\n"
    "    yy_cap = n;\n"
    "#ifndef YY_TEXT_ARRAY\n"
    "    yytext = yy_buf + yy_tok;\n"
    "#endif\n"
    "}\n"
    "\n"
    "/* reads one more character into the buffer; 0 at the end of the input */\n"
    "static int yy_fill(void)\n"
    "{\n"
    "    int c;\n"
    "    if (yy_eof)\n"
    "        return 0;\n"
    "    c = getc(yyin);\n"
    "    if (c == EOF) {\n"
    "        yy_eof = 1;\n"
    "        return 0;\n"
    "    }\n"
    "    if (yy_end + 2 > yy_cap)\n"
    "        yy_grow(yy_end + 2);\n"
    "    if (yy_end == yy_hold_pos)\n"
    "        yy_hold_char = (char)c;\n"
    "    else\n"
    "        yy_buf[yy_end] = (char)c;\n"
    "    yy_end++;\n"
    "    return 1;\n"
    "}\n"
    "\n"
    "static int input(void)\n"
    "{\n"
    "    int c;\n"
    "    if (!yy_init_done)\n"
    "        yy_init();\n"
    "    if (yy_pos == yy_end && !yy_fill())\n"
    "        return 0;\n"
    "    c = (unsigned char)(yy_pos == yy_hold_pos ? yy_hold_char : yy_buf[yy_pos]);\n"
    "    yy_pos++;\n"
    "    if (c == '\\n')\n"
    "        yylineno++;\n"
    "    yy_lastc = c;\n"
    "    return c;\n"
    "}\n"
    "\n"
    "static int unput(int c)\n"
    "{\n"
    "    if (!yy_init_done)\n"
    "        yy_init();\n"
    "    yy_restore();\n"
    "    if (yy_pos == 0) {\n"
    "        long k = yy_cap / 2 > 64 ? yy_cap / 2 : 64;\n"
    "        yy_grow(yy_end + k + 2);\n"
    "        memmove(yy_buf + k, yy_buf, (size_t)yy_end);\n"
    "        yy_pos += k;\n"
    "        yy_end += k;\n"
    "        yy_tok += k;\n"
    "    }\n"
    "    yy_buf[--yy_pos] = (char)c;\n"
    "    if (yy_tok > yy_pos)\n"
    "        yy_tok = yy_pos;\n"
    "    if (c == '\\n')\n"
    "        yylineno--;\n"
    "    return c;\n"
    "}\n"
    "\n"
    "static int yy_less(int n)\n"
    "{\n"
    "    long i;\n"
    "    if (n < 0)\n"
    "        n = 0;\n"
    "    if (n > yyleng)\n"
    "        n = yyleng;\n"
    "    yy_restore();\n"
    "    for (i = yy_tok + n; i < yy_tok + yyleng && i < yy_end; i++)\n"
    "        if (yy_buf[i] == '\\n')\n"
    "            yylineno--;\n"
    "    yy_pos = yy_tok + n;\n"
    "    yyleng = n;\n"
    "    yy_set_text();\n"
    "    return 0;\n"
    "}\n"
    "\n";

static const char *const skel_tc =
    "/* the length of the r part of a match of r/x */\n"
    "static long yy_tc_cut(int rule, long scan, long len)\n"
    "{\n"
    "    static char *yy_racc;\n"
    "    static long yy_racccap;\n"
    "    long i, j;\n"
    "    int s;\n"
    "    switch (yy_tc_kind[rule]) {\n"
    "    case 1:\n"
    "        return yy_tc_len[rule];\n"
    "    case 2:\n"
    "        return len - yy_tc_len[rule];\n"
    "    case 3:\n"
    "        if (len + 1 > yy_racccap) {\n"
    "            free(yy_racc);\n"
    "            yy_racccap = 2 * (len + 1);\n"
    "            yy_racc = (char *)malloc((size_t)yy_racccap);\n"
    "            if (!yy_racc)\n"
    "                yy_fatal(\"lex scanner: out of memory\");\n"
    "        }\n"
    "        s = yy_tc_r[rule];\n"
    "        yy_racc[0] = (char)yy_racc_st[s];\n"
    "        for (i = 0; i < len; i++) {\n"
    "            if (s >= 0)\n"
    "                s = YY_NEXT(s, yy_ec[(unsigned char)yy_buf[scan + i]]);\n"
    "            yy_racc[i + 1] = (char)(s >= 0 && yy_racc_st[s]);\n"
    "        }\n"
    "        for (i = len; i >= 0; i--) {\n"
    "            if (!yy_racc[i])\n"
    "                continue;\n"
    "            s = yy_tc_x[rule];\n"
    "            for (j = i; j < len && s >= 0; j++)\n"
    "                s = YY_NEXT(s, yy_ec[(unsigned char)yy_buf[scan + j]]);\n"
    "            if (s >= 0 && yy_xacc_st[s])\n"
    "                return i;\n"
    "        }\n"
    "        return len;\n"
    "    }\n"
    "    return len;\n"
    "}\n"
    "\n";

/*
 * The transitions, compressed: each state has a default target (its most
 * common one, often "none") and its other transitions are packed into
 * yy_nxt/yy_chk, first fit, at an offset yy_base[state].
 */
static void pack_transitions(Out *o)
{
    int n = ndstates;
    int *base = xmalloc((size_t)(n ? n : 1) * sizeof(int));
    int *def = xmalloc((size_t)(n ? n : 1) * sizeof(int));
    int *order = xmalloc((size_t)(n ? n : 1) * sizeof(int));
    int *nexc = xcalloc((size_t)(n ? n : 1), sizeof(int));
    int *cnt = xcalloc((size_t)n + 1, sizeof(int));
    int cap = 1024, size = 0, *nxt, *chk, *nextfree;
    int *exc = xmalloc((size_t)nclasses * sizeof(int));

    /* defaults */
    for (int s = 0; s < n; s++) {
        int best = -1, bestn = 0;
        for (int c = 0; c < nclasses; c++)
            cnt[dstates[s].trans[c] + 1]++;
        for (int c = 0; c < nclasses; c++) {
            int t = dstates[s].trans[c];
            if (cnt[t + 1] > bestn || (cnt[t + 1] == bestn && t < best)) {
                bestn = cnt[t + 1];
                best = t;
            }
        }
        for (int c = 0; c < nclasses; c++)
            cnt[dstates[s].trans[c] + 1] = 0;
        def[s] = best;
        for (int c = 0; c < nclasses; c++)
            if (dstates[s].trans[c] != best)
                nexc[s]++;
        order[s] = s;
    }
    /* the fullest rows first */
    for (int i = 1; i < n; i++) {
        int v = order[i], j = i - 1;
        while (j >= 0 && nexc[order[j]] < nexc[v]) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = v;
    }
    nxt = xmalloc((size_t)cap * sizeof(int));
    chk = xmalloc((size_t)cap * sizeof(int));
    nextfree = xmalloc(((size_t)cap + 1) * sizeof(int));
    for (int i = 0; i < cap; i++) {
        nxt[i] = 0;
        chk[i] = -1;
        nextfree[i] = i;
    }
    nextfree[cap] = cap;
#define GROW(need) \
    while ((need) > cap) { \
        int nc = cap * 2; \
        nxt = xrealloc(nxt, (size_t)nc * sizeof(int)); \
        chk = xrealloc(chk, (size_t)nc * sizeof(int)); \
        nextfree = xrealloc(nextfree, ((size_t)nc + 1) * sizeof(int)); \
        for (int q = cap; q < nc; q++) { nxt[q] = 0; chk[q] = -1; nextfree[q] = q; } \
        nextfree[nc] = nc; \
        cap = nc; \
    }
    for (int i = 0; i < n; i++) {
        int s = order[i], ne = 0, p, b;
        for (int c = 0; c < nclasses; c++)
            if (dstates[s].trans[c] != def[s])
                exc[ne++] = c;
        if (!ne) {
            base[s] = 0;
            continue;
        }
        /* first fit: the first exception at each free position in turn */
        p = 0;
        for (;;) {
            int r = p, ok = 1;
            while (r < cap && nextfree[r] != r)
                r = nextfree[r];
            while (p < cap && nextfree[p] != p) {   /* path compression */
                int nx = nextfree[p];
                nextfree[p] = r;
                p = nx;
            }
            p = r;
            b = p - exc[0];
            if (b < 0) {
                p++;
                GROW(p + 1);
                continue;
            }
            GROW(b + exc[ne - 1] + 2);
            for (int k = 1; k < ne; k++)
                if (chk[b + exc[k]] != -1) {
                    ok = 0;
                    break;
                }
            if (ok)
                break;
            p++;
            GROW(p + 1);
        }
        for (int k = 0; k < ne; k++) {
            int q = b + exc[k];
            nxt[q] = dstates[s].trans[exc[k]];
            chk[q] = s;
            nextfree[q] = q + 1;
            if (q + 1 > size)
                size = q + 1;
        }
        base[s] = b;
    }
#undef GROW
    /* a lookup may reach beyond the last entry: pad with nothing */
    size += nclasses;
    if (size > cap) {
        nxt = xrealloc(nxt, (size_t)size * sizeof(int));
        chk = xrealloc(chk, (size_t)size * sizeof(int));
        for (int q = cap; q < size; q++) {
            nxt[q] = 0;
            chk[q] = -1;
        }
    }
    /* check the packing */
    for (int s = 0; s < n; s++)
        for (int c = 0; c < nclasses; c++) {
            int q = base[s] + c;
            int t = chk[q] == s ? nxt[q] : def[s];
            if (t != dstates[s].trans[c])
                fatal("internal error: transition table wrong for state %d", s);
        }
    put_array(o, "yy_base", base, n);
    put_array(o, "yy_def", def, n);
    put_array(o, "yy_nxt", nxt, size);
    put_array(o, "yy_chk", chk, size);
    put(o, "#define YY_NEXT(s, c) (yy_chk[yy_base[s] + (c)] == (s) ? yy_nxt[yy_base[s] + (c)] : yy_def[s])\n");
    table_entries = size;
    free(base);
    free(def);
    free(order);
    free(nexc);
    free(cnt);
    free(nxt);
    free(chk);
    free(nextfree);
    free(exc);
}

void generate(FILE *fp)
{
    Out o = { fp, 1 };
    int i, any_tc = 0, any_generic = 0;
    const char *f;
    int fl;

    where(1, &f, &fl);
    put(&o, "/* A lexical analyzer generated by lex");
    if (*f) {
        put(&o, " from ");
        put(&o, f);
    }
    put(&o, ".  */\n\n");
    put(&o, "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\n");
    if (yytext_array)
        put(&o, "#define YY_TEXT_ARRAY 1\nextern char yytext[];\n");
    else
        put(&o, "extern char *yytext;\n");
    put(&o, "extern int yyleng;\n"
            "extern FILE *yyin, *yyout;\n"
            "extern int yylineno;\n"
            "int yylex(void);\n");
    if (opt_noyywrap)
        put(&o, "#define yywrap() 1\n");
    else
        put(&o, "int yywrap(void);\n");
    put(&o, "static int input(void);\n"
            "static int unput(int);\n"
            "static int yy_less(int);\n"
            "static int yy_start;\n"
            "static int yy_more_flag;\n"
            "\n"
            "#define BEGIN yy_start =\n"
            "#define YY_START yy_start\n"
            "#define YYSTATE YY_START\n"
            "#define REJECT goto yy_reject\n"
            "#define yymore() (yy_more_flag = 1)\n"
            "#define yyless(n) yy_less(n)\n"
            "#define yyterminate() return 0\n"
            "#define YY_BREAK break;\n");
    for (i = 0; i < nsconds; i++)
        putf(&o, "#define %s %d\n", sconds[i].name, i);
    put(&o, "\n");

    for (i = 0; i < ndefcode; i++)
        put_code(&o, &defcode[i]);
    put(&o, "\n");
    /* these may be defined by the application's code */
    put(&o, "#ifndef ECHO\n#define ECHO ((void)fwrite(yytext, 1, (size_t)yyleng, yyout))\n#endif\n");
    if (yytext_array)
        put(&o, "#ifndef YYLMAX\n#define YYLMAX 8192\n#endif\n");
    put(&o, "#ifndef YY_DECL\n#define YY_DECL int yylex(void)\n#endif\n\n");

    if (yytext_array)
        put(&o, "char yytext[YYLMAX];\n");
    else
        put(&o, "char *yytext;\n");
    put(&o, "int yyleng;\nFILE *yyin, *yyout;\nint yylineno = 1;\n\n");

    /* ---------- tables ---------- */
    for (int r = 0; r < nrules; r++)
        if (rules[r].trailing) {
            any_tc = 1;
            if (rules[r].rlen < 0 && rules[r].xlen < 0)
                any_generic = 1;
        }
    putf(&o, "#define YY_NCLASSES %d\n#define YY_NSTATES %d\n#define YY_NCONDITIONS %d\n\n", nclasses, ndstates,
         nsconds);
    {
        int *v = xmalloc((size_t)(ndstates ? ndstates : 1) * (size_t)nclasses * sizeof(int) + 256 * sizeof(int));
        int k = 0;
        put_array(&o, "yy_ec", ec, 256);
        (void)k;
        pack_transitions(&o);
        /* no transition out of a state: no need to read further */
        for (int s = 0; s < ndstates; s++) {
            int none = 1;
            for (int c = 0; c < nclasses; c++)
                if (dstates[s].trans[c] >= 0)
                    none = 0;
            v[s] = none;
        }
        put_array(&o, "yy_noout", v, ndstates);
        /* accepting rules */
        k = 0;
        for (int s = 0; s < ndstates; s++) {
            v[s] = k;
            k += dstates[s].nacc;
        }
        v[ndstates] = k;
        put_array(&o, "yy_accidx", v, ndstates + 1);
        k = 0;
        for (int s = 0; s < ndstates; s++)
            for (int a = 0; a < dstates[s].nacc; a++)
                v[k++] = dstates[s].acc[a] + 1;
        put_array(&o, "yy_acclist", v, k);
        put_array(&o, "yy_start_tab", start_state, 2 * nsconds);
        if (any_tc) {
            for (int r = 0; r < nrules; r++) {
                Rule *ru = &rules[r];
                v[r + 1] = !ru->trailing ? 0 : ru->rlen >= 0 ? 1 : ru->xlen >= 0 ? 2 : 3;
            }
            v[0] = 0;
            put_array(&o, "yy_tc_kind", v, nrules + 1);
            v[0] = 0;
            for (int r = 0; r < nrules; r++)
                v[r + 1] = !rules[r].trailing ? 0 : rules[r].rlen >= 0 ? rules[r].rlen : rules[r].xlen;
            put_array(&o, "yy_tc_len", v, nrules + 1);
            if (any_generic) {
                v[0] = -1;
                for (int r = 0; r < nrules; r++)
                    v[r + 1] = tc_rstart[r];
                put_array(&o, "yy_tc_r", v, nrules + 1);
                for (int r = 0; r < nrules; r++)
                    v[r + 1] = tc_xstart[r];
                put_array(&o, "yy_tc_x", v, nrules + 1);
                for (int s = 0; s < ndstates; s++)
                    v[s] = dstates[s].racc;
                put_array(&o, "yy_racc_st", v, ndstates);
                for (int s = 0; s < ndstates; s++)
                    v[s] = dstates[s].xacc;
                put_array(&o, "yy_xacc_st", v, ndstates);
            } else
                put(&o, "static const signed char yy_tc_r[1] = { -1 }, yy_tc_x[1] = { -1 };\n"
                        "static const signed char yy_racc_st[1] = { 0 }, yy_xacc_st[1] = { 0 };\n");
        }
        free(v);
    }
    put(&o, "\n");
    put(&o, skel_runtime);
    if (any_tc)
        put(&o, skel_tc);

    /* ---------- yylex ---------- */
    put(&o, "YY_DECL\n"
            "{\n"
            "    int yy_state, yy_rule, yy_acc = 0, yy_scan_lastc;\n"
            "    long yy_scan, yy_n, yy_len, yy_mlen, yy_i;\n");
    for (i = 0; i < nprecode; i++)
        put_code(&o, &precode[i]);
    put(&o, "    if (!yy_init_done)\n"
            "        yy_init();\n"
            "    if (0) {\n"
            "        /* keep compilers quiet about routines and labels an application may not use */\n"
            "        (void)input();\n"
            "        (void)unput(0);\n"
            "        (void)yy_less(0);\n"
            "        goto yy_reject;\n"
            "    }\n"
            "    for (;;) {\n"
            "        yy_restore();\n"
            "        if (yy_more_flag)\n"
            "            yy_more_flag = 0;\n"
            "        else {\n"
            "            yy_tok = yy_pos;\n"
            "            /* discard the text before the token */\n"
            "            if (yy_tok > 0 && yy_tok > yy_cap / 4) {\n"
            "                memmove(yy_buf, yy_buf + yy_tok, (size_t)(yy_end - yy_tok));\n"
            "                yy_end -= yy_tok;\n"
            "                yy_pos -= yy_tok;\n"
            "                yy_tok = 0;\n"
            "            }\n"
            "        }\n"
            "        yy_scan = yy_pos;\n"
            "        yy_scan_lastc = yy_lastc;\n"
            "        if (yy_pos == yy_end && !yy_fill()) {\n"
            "            /* the end of the input */\n"
            "            yy_eof = 0;\n"
            "            if (yywrap())\n"
            "                return 0;\n"
            "            continue;\n"
            "        }\n"
            "        if (yy_start < 0 || yy_start >= YY_NCONDITIONS)\n"
            "            yy_start = 0;\n"
            "        yy_state = yy_start_tab[2 * yy_start + (yy_lastc == '\\n')];\n"
            "        yy_n = 0;\n"
            "        if (yy_state >= 0) {\n"
            "            yy_hist[0] = yy_state;\n"
            "            for (;;) {\n"
            "                int yy_next;\n"
            "                if (yy_noout[yy_state])\n"
            "                    break;\n"
            "                if (yy_scan + yy_n == yy_end && !yy_fill())\n"
            "                    break;\n"
            "                yy_next = YY_NEXT(yy_state, yy_ec[(unsigned char)yy_buf[yy_scan + yy_n]]);\n"
            "                if (yy_next < 0)\n"
            "                    break;\n"
            "                yy_state = yy_next;\n"
            "                yy_n++;\n"
            "                if (yy_n >= yy_histcap) {\n"
            "                    int *h = (int *)realloc(yy_hist, (size_t)yy_histcap * 2 * sizeof *yy_hist);\n"
            "                    if (!h)\n"
            "                        yy_fatal(\"lex scanner: out of memory\");\n"
            "                    yy_hist = h;\n"
            "                    yy_histcap *= 2;\n"
            "                }\n"
            "                yy_hist[yy_n] = yy_state;\n"
            "            }\n"
            "        }\n"
            "        /* the longest match; among equally long ones the first rule */\n"
            "        for (yy_len = yy_n; yy_len > 0; yy_len--)\n"
            "            if (yy_accidx[yy_hist[yy_len]] < yy_accidx[yy_hist[yy_len] + 1])\n"
            "                break;\n"
            "        if (yy_len > 0)\n"
            "            yy_acc = yy_accidx[yy_hist[yy_len]];\n"
            "    yy_found:\n"
            "        if (yy_len == 0) {\n"
            "            yy_rule = 0;   /* no rule matches: the default action */\n"
            "            yy_mlen = 1;\n"
            "        } else {\n"
            "            yy_rule = yy_acclist[yy_acc];\n"
            "            yy_mlen = yy_len;\n");
    if (any_tc)
        put(&o, "            if (yy_tc_kind[yy_rule])\n"
                "                yy_mlen = yy_tc_cut(yy_rule, yy_scan, yy_len);\n");
    put(&o, "        }\n"
            "        yy_pos = yy_scan + yy_mlen;\n"
            "        yyleng = (int)(yy_pos - yy_tok);\n"
            "        for (yy_i = yy_scan; yy_i < yy_pos; yy_i++)\n"
            "            if (yy_buf[yy_i] == '\\n')\n"
            "                yylineno++;\n"
            "        if (yyleng > 0)\n"
            "            yy_lastc = (unsigned char)yy_buf[yy_pos - 1];\n"
            "        yy_set_text();\n"
            "        switch (yy_rule) {\n");
    for (int r = 0; r < nrules; r++) {
        putf(&o, "        case %d:\n", r + 1);
        if (!rules[r].action)
            continue;
        line_to(&o, rules[r].action_line);
        put(&o, rules[r].action);
        put(&o, "\n");
        line_back(&o);
        put(&o, "            break;\n");
        for (int k = 0; k < nmidcode; k++)
            if (midcode_after[k] == r)
                put_code(&o, &midcode[k]);
    }
    put(&o, "        default:\n"
            "            ECHO;\n"
            "            break;\n"
            "        }\n"
            "        continue;\n"
            "    yy_reject:\n"
            "        /* the next choice for the same input: the next rule, then shorter matches */\n"
            "        yy_restore();\n"
            "        for (yy_i = yy_scan; yy_i < yy_pos; yy_i++)\n"
            "            if (yy_buf[yy_i] == '\\n')\n"
            "                yylineno--;\n"
            "        yy_lastc = yy_scan_lastc;\n"
            "        if (yy_len > 0) {\n"
            "            yy_acc++;\n"
            "            if (yy_acc >= yy_accidx[yy_hist[yy_len] + 1]) {\n"
            "                for (yy_len--; yy_len > 0; yy_len--)\n"
            "                    if (yy_accidx[yy_hist[yy_len]] < yy_accidx[yy_hist[yy_len] + 1])\n"
            "                        break;\n"
            "                if (yy_len > 0)\n"
            "                    yy_acc = yy_accidx[yy_hist[yy_len]];\n"
            "            }\n"
            "        }\n"
            "        goto yy_found;\n"
            "    }\n"
            "}\n");

    if (subroutines) {
        put(&o, "\n");
        line_to(&o, subroutines_line);
        put(&o, subroutines);
        if (*subroutines && subroutines[strlen(subroutines) - 1] != '\n')
            put(&o, "\n");
    }
}
