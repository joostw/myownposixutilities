# yacc — yet another compiler compiler (POSIX.1-2024)

A from-scratch implementation of the yacc parser generator as
specified in XCU yacc (POSIX.1-2024), in C99 with no dependencies
beyond libc, together with the yacc library (liby.a: main and yyerror).

## Build and test

    make           # builds ./yacc and ./liby.a
    make test      # the tests (sh test_yacc.sh): regression suite
    make difftest  # compares with bison -y and byacc (needs both)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

`make install` also puts liby.a in `$(PREFIX)/lib`, for `c17 ... -l y`.

## What is implemented

Options -b file_prefix, -d, -l, -p sym_prefix, -t and -v, following the
utility syntax guidelines (grouped flags, attached or separate option
arguments, --).

The grammar:
  * Declarations: %token, %left, %right, %nonassoc, %type with `<tag>`,
    token numbers after the first appearance of a name, %start, %union,
    and %{ ... %} blocks (copied in order; "%}" inside comments, strings
    and character constants is skipped).
  * Rules with |, an optional ; (also ";" followed by "|", as the formal
    grammar allows), %prec with a name or literal, followed or preceded
    by the action, empty rules, and actions anywhere in a rule.  A
    mid-rule action becomes an empty rule for a new nonterminal ($$1,
    $$2, ...), numbered before the rule containing it.
  * Literals with all the C escape sequences (\n \t \v \b \r \f \a `\\`
    `\'` `\"` `\?` octal and \x hex).
  * Names with letters, digits, periods and underscores.  Comments are
    /* */ (and // as an extension).
  * The programs section, or none (the second %% may be omitted).

Actions: $$, $n, $0 and negative $n, `$<tag>$` and `$<tag>n`.  With tags in
use, the types are checked: $$ or $n without a type is an error, as is a
default action ($$ = $1) whose types differ.  $n beyond the components
before the action is an error.  "$" in strings, character constants and
comments is left alone.

Token numbers: literals are their character codes; error is 256 unless
renumbered with %token; other tokens get numbers from 257 in order of
declaration, skipping numbers assigned explicitly.  Two tokens with the
same number are an error.  %token NAME 0 names the end marker.  Very
large token numbers work (the parser uses a binary search when the
largest is above 65535).

Tables: LALR(1) lookaheads computed with the DeRemer-Pennello relations;
conflicts are resolved as the standard says (precedence and
associativity, then shift, then the earlier rule) and reported on
standard error.  Resolutions by precedence are not counted.  Each state
has a default reduction (the most frequent one) unless it can shift the
error token.  A state whose only actions besides errors are reductions
by one rule never reads a lookahead (with one exception, below).
States that conflict resolution leaves unreachable are dropped, with
their conflicts, as in bison.  The tables are packed, sharing
identical rows, and checked against the actions they encode before they
are written.

The code file contains, in this order: YYEMPTY ((-2)), YYEOF (0) and the
token #defines; the YYDEBUG default (1 with -t, else 0, unless already
defined); the %{ %} code and the YYSTYPE union in grammar order; a
typedef of YYSTYPE as int if there is no %union and no tag; int yychar,
YYSTYPE yylval, yynerrs and yydebug; the prototypes of yyerror (guarded
by #if !defined(yyerror)), yylex (likewise) and yyparse; the tables and
yyparse; then the programs section.  #line directives point into the
grammar for copied code and back into the code file afterwards; -l
removes them.

The header (-d) has the token #defines (only for names that are C
identifiers), YYSTYPE, extern yylval and the yyparse prototype; with -p
the names are written with the prefix.

-p renames yyparse, yylex, yyerror, yylval, yychar, yydebug and yynerrs
with #define; everything else in the code file is static, so several
parsers can be linked into one program.

The parser:
  * yylex results <= 0 become YYEOF.
  * The stacks start at YYINITDEPTH (200) entries and grow up to
    YYMAXDEPTH (10000), both overridable with -D; beyond that, yyerror
    is called with "yacc stack overflow" and yyparse returns 2.
  * Error recovery follows the standard exactly:
    - a syntax error calls yyerror("syntax error") unless the parser is
      still recovering (fewer than three tokens shifted since the last
      error, and no yyerrok);
    - if no token was shifted since the last error, the lookahead is
      discarded, or the parse fails at the end of input;
    - otherwise states are popped until one can shift error;
    - YYERROR starts the same popping without calling yyerror.
  * YYACCEPT, YYABORT, yyerrok, yyclearin and YYRECOVERING() work in
    actions.
  * With YYDEBUG, setting yydebug traces reads, shifts, reductions and
    error recovery to standard error.

The description file (-v) lists the rules, every state (kernel items,
actions, default reduction, gotos, conflicts and how precedence resolved
them), the conflict totals per state, a summary with the table sizes and
limits, and the symbols with their numbers.  After errors in the grammar
it still contains a summary.

Diagnostics name the file and line.  An error in the grammar exits with
status 1 without writing the code file; usage and I/O errors exit with
status 2.

## Choices where the standard leaves room

* Limits: none besides memory (the description file says so).
* The token error has no #define (it would clash with ordinary C names
  called "error"); YYERRCODE gives its number.
* Without %union but with tags, YYSTYPE is left to the application (the
  standard's alternative of a typedef in a header).  YYSTYPE_IS_DECLARED
  suppresses the generated definition, as in bison.
* The value of an empty rule without an action is zeroed.
* liby's main() returns failure when yyparse fails.
* A token with a shift and several reductions, one of them resolved by
  precedence: precedence is applied to each reduction first, then the
  earliest remaining rule wins (as in bison; byacc applies them in
  order and can choose a later rule).
* The standard says a parser never reads a lookahead in a state whose
  only actions other than errors are reductions by one rule, and also
  that a nonassociative use of a %nonassoc token is an error.  In a
  state such as "e : e '<' e ." the two collide: reducing without a
  lookahead lets "a < b < c" through.  Here the explicit error actions
  made by %nonassoc count as actions, so such a state reads the
  lookahead and "a < b < c" is a syntax error, as in bison (byacc
  follows the first rule and accepts it).  Every other state without a
  choice is reduced without reading.
* Conflict counts are per token, as in bison: one shift/reduce conflict
  for a token with a shift and any reductions, and n-1 reduce/reduce
  conflicts for n reductions.
* Extensions:
  - multi-character and "string" literals become tokens of their own,
    numbered like named tokens (the standard's own at and localedef
    grammars use them); a one-character string is the character literal;
  - %expect N and %expect-rr N silence the conflict report when the
    counts match;
  - %term, %binary and the historical %< %> %2 %0 %= spellings, and
    keywords in either case.
* A non-terminal declared with %type but never used only gets a warning.
  Rules that are never reduced (because of conflicts or unreachable)
  get a warning and are listed in the description file.

## Testing

* tests/run.py, the regression suite (20 cases, outputs frozen in
  tests/expected).  Generated parsers are compiled and run, covering:
  - precedence, %prec and error recovery;
  - %union, `$<tag>` in mid-rule actions, $0 and $-1;
  - two parsers linked together with -p and -b, and a separate lexer
    using the header;
  - -t debugging, #line directives and -l;
  - YYACCEPT, YYABORT, YYERROR, yyclearin and YYRECOVERING();
  - token numbering, the end marker by name, large token numbers;
  - liby, negative yylex results, stack growth and overflow;
  - lookahead timing, error recovery step by step, the description
    file, option syntax;
  - 33 errors in grammars and 5 usage errors;
  - the grammars printed in POSIX.1-2024 itself (awk, sh, bc, at,
    localedef, regular expressions and yacc's own);
  - strict ISO compilation of the code file (C99, C11 and C17 with
    -pedantic -Wall -Wextra -Werror).
* tests/specdiff.py: random sentences of those POSIX grammars, and
  mutated ones, are parsed by this yacc's parsers and by bison's; the
  sequences of reductions, the errors and the results are identical
  (2,200 inputs, no differences).  The same holds against byacc, for which yacc has a
  test-only switch (YACC_TEST_DEFAULTS=byacc) that selects byacc's
  choice of default reductions so that error detection and recovery can
  be compared step by step.
* tests/randgram.py: random grammars (literals, named tokens,
  precedence, %prec, mid-rule actions with values, error rules with
  and without yyerrok), run on sentences, mutated sentences and random
  strings.  The output shows the reductions, the values, the yyerror
  calls and the result.  Conflict counts are compared with bison.
  The last 510 grammars (19,000 parses) gave:
  - bison, grammars without error rules: no differences;
  - conflict counts: no differences;
  - byacc, every grammar (with byacc's default reductions): no
    differences except a byacc quirk: byacc accepts only right after
    the reduction to the start symbol, so when error recovery has just
    discarded a token in its final state it fails at the end of input
    where the standard's algorithm accepts.
  Grammars with error rules do differ from bison by design: after a
  token is discarded, bison pops states and shifts error again, while
  the standard (and byacc, and this yacc) resumes parsing in the same
  state.  Cyclic random grammars that make every parser loop are
  skipped.
* Under AddressSanitizer and UBSan: the regression suite, the POSIX
  grammars, and 16,000 mutated grammars (fuzzing the reader and the
  table construction, with the packed tables checked against the
  actions every time) found nothing.
* Speed: 30 copies of the awk grammar (6,211 rules, 12,843 states) take
  0.1 s; byacc takes 0.75 s and bison 1.1 s.

## Files

| file | contents |
|------|----------|
| `main.c` | options, file names, the order of the work |
| `reader.c` | the grammar: lexical analysis, declarations, rules, actions and $ translation, packing symbols and rules |
| `lalr.c` | LR(0) states and LALR(1) lookaheads |
| `tables.c` | parser actions, conflict resolution, default reductions |
| `output.c` | the code file and header: tables, packing, the parser |
| `verbose.c` | the description file |
| `util.c` | memory, strings, diagnostics |
| `liby/` | the yacc library |
| `tests/` | the test suites and their data (see Testing) |
| `test_yacc.sh` | runs the tests: `make test`, or `sh test_yacc.sh` |
| `Makefile` | build, test, difftest, install, clean |
