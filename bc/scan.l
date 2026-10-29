%{
/*
 * scan.l - the tokens of bc (POSIX.1-2024, Lexical Conventions in bc).
 * lex builds the scanner (scan.c) from this file.
 */
#include "bc.h"
#include "bc.tab.h"

#define YY_DECL static int scan_token(void)
static int scan_token(void);

static char *strip_continuations(const char *s, int len);
static int keyword(const char *s);
static void count_lines(const char *s, int len);
%}
%x COMMENT
%%
"/*"                    BEGIN COMMENT;
<COMMENT>"*/"           BEGIN INITIAL;
<COMMENT>\n             cur_line++;
<COMMENT>[^*\n]+        ;
<COMMENT>"*"            ;
"#".*                   ;
\\\n                    cur_line++;
[ \t\f\v\r]+            ;
\n                      return NEWLINE;
\"[^"]*\"               {
                            count_lines(yytext, yyleng);
                            yylval.s = malloc((size_t)yyleng - 1);
                            if (!yylval.s)
                                bc_nomem();
                            memcpy(yylval.s, yytext + 1, (size_t)yyleng - 2);
                            yylval.s[yyleng - 2] = 0;
                            return STRING;
                        }
\"[^"]*                 {
                            count_lines(yytext, yyleng);
                            bc_error("unterminated string");
                        }
([0-9A-F](\\\n)*)+("."(\\\n)*([0-9A-F](\\\n)*)*)?   |
"."(\\\n)*([0-9A-F](\\\n)*)+  {
                            count_lines(yytext, yyleng);
                            yylval.s = strip_continuations(yytext, yyleng);
                            return NUMBER;
                        }
[a-z][a-z0-9_]*         {
                            int k = keyword(yytext);
                            if (k)
                                return k;
                            yylval.s = malloc((size_t)yyleng + 1);
                            if (!yylval.s)
                                bc_nomem();
                            memcpy(yylval.s, yytext, (size_t)yyleng + 1);
                            return LETTER;
                        }
"=="                    { yylval.op = R_EQ; return REL_OP; }
"<="                    { yylval.op = R_LE; return REL_OP; }
">="                    { yylval.op = R_GE; return REL_OP; }
"!="                    { yylval.op = R_NE; return REL_OP; }
"<"                     { yylval.op = R_LT; return REL_OP; }
">"                     { yylval.op = R_GT; return REL_OP; }
"="                     { yylval.op = A_SET; return ASSIGN_OP; }
"+="                    { yylval.op = A_ADD; return ASSIGN_OP; }
"-="                    { yylval.op = A_SUB; return ASSIGN_OP; }
"*="                    { yylval.op = A_MUL; return ASSIGN_OP; }
"/="                    { yylval.op = A_DIV; return ASSIGN_OP; }
"%="                    { yylval.op = A_MOD; return ASSIGN_OP; }
"^="                    { yylval.op = A_POW; return ASSIGN_OP; }
"*"                     { yylval.op = E_MUL; return MUL_OP; }
"/"                     { yylval.op = E_DIV; return MUL_OP; }
"%"                     { yylval.op = E_MOD; return MUL_OP; }
"++"                    { yylval.op = 1; return INCR_DECR; }
"--"                    { yylval.op = -1; return INCR_DECR; }
"&&"                    return AND;
"||"                    return OR;
[-+(),;\[\]^{}!]        return yytext[0];
.                       {
                            if (isprint((unsigned char)yytext[0]))
                                bc_error("illegal character: %c", yytext[0]);
                            else
                                bc_error("illegal character: \\%03o", (unsigned char)yytext[0]);
                        }
%%
static const struct {
    const char *name;
    int token;
} keywords[] = {
    { "auto", Auto }, { "break", Break }, { "define", Define }, { "ibase", Ibase }, { "if", If },
    { "for", For }, { "length", Length }, { "obase", Obase }, { "quit", Quit }, { "return", Return },
    { "scale", Scale }, { "sqrt", Sqrt }, { "while", While },
    /* extensions */
    { "else", Else }, { "continue", Continue }, { "halt", Halt }, { "print", Print },
};

static int keyword(const char *s)
{
    for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++)
        if (!strcmp(s, keywords[i].name))
            return keywords[i].token;
    return 0;
}

static void count_lines(const char *s, int len)
{
    for (int i = 0; i < len; i++)
        if (s[i] == '\n')
            cur_line++;
}

static char *strip_continuations(const char *s, int len)
{
    char *r = malloc((size_t)len + 1), *q = r;
    if (!r)
        bc_nomem();
    for (int i = 0; i < len; i++) {
        if (s[i] == '\\' && i + 1 < len && s[i + 1] == '\n') {
            i++;
            continue;
        }
        *q++ = s[i];
    }
    *q = 0;
    return r;
}

/* ---------- the input: the files, then the standard input ---------- */

static const char **files;
static int nfiles, next_file;
static int last_token = NEWLINE;
static int at_eof;
static int started;

void scanner_add_file(const char *name)
{
    files = realloc(files, (size_t)(nfiles + 1) * sizeof *files);
    if (!files)
        bc_nomem();
    files[nfiles++] = name;
}

/* opens the next input; 0 when there is none */
int scanner_next_input(void)
{
    if (yyin && yyin != stdin)
        fclose(yyin);
    yyin = NULL;
    if (next_file < nfiles) {
        const char *name = files[next_file++];
        yyin = fopen(name, "r");
        if (!yyin) {
            fprintf(stderr, "bc: cannot open %s: %s\n", name, strerror(errno));
            bc_quit(2);
        }
        cur_file = name;
        cur_line = 1;
        started = 0;
        return 1;
    }
    if (next_file == nfiles) {
        next_file++;
        yyin = stdin;
        cur_file = "(standard input)";
        cur_line = 1;
        started = 0;
        return 1;
    }
    return 0;
}

int yywrap(void)
{
    if (YY_START == COMMENT) {
        bc_error("end of file in a comment");
        BEGIN INITIAL;
    }
    return !scanner_next_input();
}

/*
 * The tokens for the parser.  A NEWLINE is supplied at the end of the
 * input when the last line has none, so that it is executed.
 */
int yylex(void)
{
    int t;
    if (at_eof)
        return 0;
    /* a newline counts once the parser has it: messages about the line
       it ends then give that line */
    if (last_token == NEWLINE && started)
        cur_line++;
    started = 1;
    t = scan_token();
    if (t == 0) {
        at_eof = 1;
        if (last_token != NEWLINE) {
            last_token = NEWLINE;
            return NEWLINE;
        }
        return 0;
    }
    last_token = t;
    return t;
}
