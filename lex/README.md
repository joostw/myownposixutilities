# lex — generate programs for lexical tasks (POSIX.1-2024)

A from-scratch implementation of the lex scanner generator as specified
in XCU lex (POSIX.1-2024), in C99 with no dependencies beyond libc,
together with the lex library (libl.a: main and yywrap).

## Build and test

    make           # builds ./lex and ./libl.a
    make test      # the tests (sh test_lex.sh): regression suite
    make difftest  # compares with flex -X (needs flex)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

`make install` also puts libl.a in `$(PREFIX)/lib`, for `c17 ... -l l`.

## What is implemented

Options -t (to standard output), -n and -v (statistics, on standard
error with -t), and any number of file operands, concatenated, with -
or no operand for standard input.

The lex source:
  * Definitions section:
    - indented lines and %{ %} blocks are copied (a comment starting
      in column 1 is copied too);
    - "name substitute" definitions;
    - %s and %x start conditions (any word beginning with s/S or x/X);
    - %array and %pointer;
    - the table sizes %p %n %a %e %k %o, which are accepted and
      ignored.
  * Rules section:
    - code before the first rule is placed at the start of yylex();
    - code after a rule is copied where it cannot run, as historical
      lex does;
    - an action is the rest of the line, continued while braces are
      open; | shares the next rule's action.
  * User subroutines are copied after yylex().
  * Diagnostics name the file (of several) and the line.

Regular expressions, with lex's additions and precedence:
  * characters, \ escapes (`\\` \a \b \f \n \r \t \v, octal \ooo, \x with
    all the hex digits that follow, \c for c), ".", bracket expressions
    with ranges, negation, [:class:] (POSIX locale), [=c=] and [.c.];
  * "..." strings, which bind like one atom ("ab"* is (ab)*);
  * ( ), {name} (substituted as if in parentheses, but not in brackets
    or quotes), * + ?, alternation, intervals {m}, {m,} and {m,n}.  As
    the standard's precedence table says, an interval binds below
    concatenation: ab{2} is abab, and it applies to the concatenation
    before it within its alternative;
  * `<sc>r`, `<sc1,sc2>r`; ^ at the start and $ at the end apply to the
    whole expression ("^abc|def$" is abc or def on a line by itself);
  * r/x trailing context.  The match is the longest r x, as for any
    rule; yytext is r.  When r or x has a fixed length the split
    follows from it; otherwise the scanner finds the split with
    automata for r and x, taking the longest r that leaves a match of
    x.  $ is trailing context of \n.

Matching: the longest match, and the first rule among equally long ones.
Unmatched input is copied to yyout one character at a time.  The tables
are a minimized DFA over byte equivalence classes, with one start state
per start condition and one for the beginning of a line.  The
transitions are packed (a default per state, the rest first fit) and
checked against the automaton before they are written.

The scanner (lex.yy.c):
  * yytext (a pointer into the buffer by default, or with %array a
    char yytext[YYLMAX], YYLMAX 8192 unless defined), int yyleng,
    FILE *yyin and *yyout (standard input and output by default), and
    yylineno.
  * The special actions ECHO, REJECT, BEGIN (also BEGIN(x)) and |;
    yymore(), yyless(n), input(), unput(c), yywrap().  INITIAL and 0
    name the initial condition.
  * REJECT goes to the next choice for the same input: the next rule
    that matched the same text, then shorter matches.
  * input() returns 0 at the end of input; unput() can push back any
    number of characters.
  * At the end of input yywrap() is called; if it returns 0, scanning
    continues with the new yyin.  After yylex() returns 0, a later call
    tries yyin again (useful on a terminal).
  * The scanner reads only as far as a match needs, so it works
    interactively on terminals and pipes.
  * Input can contain any byte, NUL included; tokens can be of any
    length with %pointer.
  * Names: all begin with yy or YY, except input, unput and the
    macros the standard gives (ECHO, REJECT, BEGIN, INITIAL) and the
    start condition names.
  * The code is ISO C (checked with C99, C11 and C17 -pedantic -Wall
    -Wextra -Werror).

## Choices where the standard leaves room

* yytext is a pointer by default.
* Statistics are written only with -v; table-size declarations do not
  turn them on, and there are no fixed table limits.
* An empty match is never taken (the default action copies a character
  instead), as in flex.
* A ^ rule is active when the last character consumed (by a match or by
  input()) was a newline, or at the start of input; unput() and yyless()
  do not change this, as in flex.
* Ambiguous trailing context (the standard leaves it unspecified when
  the end of r can match the start of x): the longest r, unless a part
  has a fixed length.
* A rule without an action gets a warning and discards its match.
* yylineno is always maintained (unput of a newline and yyless take
  lines back).
* Messages go to standard error; the exit status is 1 for errors in the
  source and 2 for usage and I/O errors.
* Accepted where the standard says "undefined" or flex is stricter:
  - empty alternatives such as (a|) and r{0};
  - a { that does not start a definition or interval is an ordinary
    character, as are *, + and ? with nothing to repeat, and ^, $, <,
    > away from their special places;
  - a hex escape with more than two digits (\x0ab is one character,
    as the standard says).
* Extensions:
  - %option noyywrap (and a few flex options, others warned about);
  - `<*>` for all start conditions;
  - YY_START/YYSTATE, yyterminate(), YY_BREAK and YY_DECL;
  - ECHO, YYLMAX and YY_DECL can be defined in the definitions section.

## Testing

* tests/difftest.py: 49 cases compiled with this lex and with flex -X
  (flex's POSIX mode) and run on their inputs; 45 give identical output.
  The cases include:
  - the standard's Pascal example and a C lexer;
  - classes, brackets, quoting, escapes, intervals and definitions;
  - ^ and $, fixed and variable trailing context;
  - inclusive, exclusive and listed start conditions;
  - REJECT, yymore, yyless, input, unput and yywrap with several files;
  - %array, NUL bytes, 100,000-character tokens and lexers with many
    keywords.
  The other 4 differ because of flex:
  - flex rejects empty alternatives;
  - flex takes only two hex digits;
  - with %array, flex drops the yymore text when the next match has
    trailing context;
  - flex misses the fixed length of an alternation, so with
    variable trailing context it splits the match wrongly.
* tests/randlex.py: random lex sources (expressions of all kinds, ^, $,
  trailing context, start conditions, and actions using BEGIN, REJECT,
  yymore, yyless, unput and input) and random inputs, compared with
  flex -X.  Trailing context is generated only where the split is
  determined.  The last 1,500 sources (34,000 runs) gave no
  differences.
* tests/run.py: the regression suite (outputs frozen in tests/expected):
  - options and several files;
  - diagnostics for 30 erroneous sources;
  - strict ISO compilation of the scanner;
  - interactive reading through a pipe;
  - #line directives and yylineno;
  - %array and YYLMAX, overriding ECHO, YYLMAX and YY_DECL;
  - code in the rules section, libl, and the C lexer on C code.
* Under AddressSanitizer and UBSan (lex and the generated scanners):
  the curated cases and the regression suite, and 10,000 mutated lex
  sources (fuzzing the reader and the automaton construction), found
  nothing.
* Speed: a word-count scanner reads 21 MB in 0.25 s (flex: 0.19 s).  A
  3,000-keyword lexer (14,123 DFA states) is generated in 0.2 s; its
  395,444 transitions pack into 47,326 entries.

## Files

| file | contents |
|------|----------|
| `main.c` | options, reading the input files, statistics |
| `scan.c` | the lex source: sections, definitions, rules and actions |
| `re.c` | regular expressions: parsing patterns into trees |
| `dfa.c` | NFA, equivalence classes, subset construction, minimization |
| `gen.c` | lex.yy.c: tables, packing, the scanner skeleton |
| `util.c` | memory, strings, diagnostics |
| `libl/` | the lex library |
| `tests/` | the test suites and their data (see Testing) |
| `test_lex.sh` | runs the tests: `make test`, or `sh test_lex.sh` |
| `Makefile` | build, test, difftest, install, clean |
