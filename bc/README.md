# bc — arbitrary-precision arithmetic language (POSIX.1-2024)

An implementation of the bc arbitrary-precision calculator as specified
in XCU bc (POSIX.1-2024), in C99 plus POSIX threads.  The grammar is a
yacc grammar (bc.y, close to the standard's own grammar) and the tokens
are a lex scanner (scan.l); both were generated with the yacc and lex
of this collection.  The generated bc.tab.c, bc.tab.h and scan.c are
included, so building needs only a C compiler.

## Build and test

    make           # builds ./bc
    make test      # the tests (sh test_bc.sh): regression and terminal tests
    make difftest  # compares with GNU bc (needs GNU bc)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

To regenerate the parser and scanner, delete the generated files and run
`make`; it uses `yacc` and `lex` from PATH, or `make YACC=... LEX=...` (for
instance the ones in `../yacc` and `../lex`).

Usage: bc [-l] [file...].  The files are read in order, then the
standard input.  -l loads the math library and sets scale to 20.

## What is implemented

The whole language of the standard:
  * numbers of any length in ibase 2..16, with `\-newline` continuations;
    variables, one-dimensional arrays, the registers scale, ibase and
    obase; strings; /* */ comments;
  * all the operators with the standard's precedence and
    associativity, including the compound assignments and ++/--;
  * the scale rules of the standard for every operator: + and - give
    the larger scale; * gives min(a+b, max(scale, a, b)); / gives scale;
    % is `a-(a/b)*b` computed with scale max(scale+scale(b), scale(a));
    ^ gives `min(a*|b|`, max(scale, a)) (1/a^|b| for a negative
    exponent); sqrt gives max(scale, scale(x)); length() and scale();
  * if, while, for, break, quit, return, { } blocks, function
    definitions with parameters, array parameters (passed by value)
    and auto variables and arrays, with the dynamic scoping of the
    standard;
  * output in any obase from 2 to BC_BASE_MAX; above 16 each digit is a
    decimal number preceded by a space.  Long numbers are split with a
    \ and newline at 70 columns (BC_LINE_LENGTH sets the width, 0 for no
    splitting);
  * the math library functions s, c, a, l, e and j, which can be
    redefined by the program.

Quit takes effect when it is read, even inside a function definition or
an untaken branch, as the standard says.

Extensions (all also in GNU bc; none changes the meaning of a strictly
conforming program):
  * else, continue, halt (stops when executed), print with a list of
    expressions and strings (escapes \a \b \f \n \q \r \t `\\`);
  * relational expressions anywhere, and !, && and ||;
  * names of more than one letter ([a-z][a-z0-9_]*) and # comments;
  * newlines after the ) of if, while and for, and after the { of a
    function;
  * return expr without parentheses; empty clauses in for;
  * a last line without a newline is still executed.

## Choices where the standard leaves room

  * Numbers are kept as a sign, a decimal scale and a binary mantissa in
    base 10^9; division is Knuth's algorithm D and square root is
    Newton's method, so all operations are exact before truncation.
  * The math library is native C working with 12 guard digits and
    truncating to scale, so results are correct to the last digit in
    practice (see below); it is also much faster than a bc-coded
    library.  pi for s and c is computed by Machin's formula and cached.
  * Limits: BC_BASE_MAX 999999999, BC_DIM_MAX 16777215, BC_SCALE_MAX
    and BC_STRING_MAX 2147483647.  An out-of-range assignment to scale,
    ibase (2..16) or obase is clamped with a warning.  A non-integer
    exponent is truncated with a warning.
  * Digits greater than ibase: a lone digit (A..F, as in "ibase=A") has
    its face value; in a longer number such a digit counts as ibase-1,
    as in GNU bc.
  * length(x) for |x| < 1 is scale(x) (1 for 0), as in GNU bc.
  * An assignment in parentheses, "(a = 5)", prints its value.
  * Recursion: the program runs on a thread with a large stack (up to
    1 GB, falling back to smaller sizes) and the stack use is checked,
    so very deep recursion (tens of thousands of calls) works and too
    deep recursion is a runtime error rather than a crash.
  * Errors: syntax errors skip to the end of the line; runtime errors
    (divide by zero, sqrt of a negative number, array index out of
    range, too deep recursion, math library domain errors) abandon the
    current line or function call, undo the function frames, and
    continue with the next input.  Messages name the file and line.
  * Exit status: 0, or 1 when an error was reported; 2 for a usage
    error or a file that cannot be opened.
  * When both the standard input and standard output are terminals, an
    interrupt (SIGINT) stops the running computation and bc reads the
    next line; otherwise SIGINT has its default action.

## Differences from GNU bc

The differential tests found these, all of which are deliberate:
  * sqrt(0) and sqrt(1): GNU gives scale 0; the standard (and this bc)
    gives max(scale, scale(x)).  (BC_TEST_GNU_SQRT=1 imitates GNU, for
    the differential tests only.)
  * GNU's math library is inaccurate when scale is small or the
    argument is large: with scale=3, GNU gives s(588) = -.504 (true
    -.4987...) and c(5) = .282 (true .2836...); with scale=20, GNU's
    s(117601) ends ...618 (true ...621366...).  This bc gives the true
    value truncated.
  * GNU sometimes prints "-0" (e.g. -(5920^-1)^7 with scale=20); this bc
    prints 0.
  * GNU rejects return () (which is in the standard's grammar) and a
    last line without a newline; this bc accepts both.
  * l(x) for x <= 0 is a runtime error here; GNU's library returns
    -(10^scale - 1) for l(0).
  * Unbounded recursion: this bc reports "function calls nested too
    deeply" and goes on; GNU bc grows until it is killed.
  * GNU accepts ibase up to 36; this bc clamps it to 16.
  * GNU exits 0 after a runtime error.

## Testing

  * tests/difftest.py: 45 curated programs (the standard's examples,
    the scale rules, bases, line splitting, functions, scoping, arrays,
    control flow, errors, quit, the math library, extensions, files,
    big numbers) compared with GNU bc 1.07: 43 identical, 2 known
    differences (above).
  * tests/randbc.py: random programs with functions, arrays, loops,
    bases and (with -l) the math library, compared with GNU bc.  About
    4400 programs over several seeds; every remaining difference was
    examined and is one of the GNU behaviours above (inaccuracy at
    small scale or large argument, "-0", l(0), unbounded
    recursion).
  * tests/run.py (make test): a frozen regression suite of error
    messages, registers, the POSIX sqrt scale, options, deep recursion,
    BC_LINE_LENGTH, and regenerating the parser and scanner with bison
    and flex.
  * tests/pty_tests.py: interactive use on a pseudo-terminal: immediate
    output, interrupting a loop, recovery after an error, end of file,
    a function typed over several lines.
  * Everything above also run with AddressSanitizer and UBSan, and
    tests/fuzz.py ran 6800 mutated programs on the sanitized build:
    no crashes, sanitizer reports or hangs beyond the time limit of
    deliberately endless programs.

Speed, compared with GNU bc 1.07 (same output):

    2^100000                      0.18 s     GNU 0.16 s
    scale=2000; 4*a(1)            0.24 s     GNU 2.95 s
    scale=10000; sqrt(2)          0.58 s     GNU 3.4 s
    scale=1000; e(100)            0.008 s    GNU 1.04 s
    3^20000 / 7^9000              0.07 s     GNU 0.44 s

## Files

| file | contents |
|------|----------|
| `bc.y` | grammar and parse actions (yacc) |
| `scan.l` | tokens and input handling (lex) |
| `bc.h` | syntax tree and interfaces |
| `exec.c` | interpreter: symbols, scoping, evaluation, output |
| `num.c`, `num.h` | arbitrary-precision decimal arithmetic |
| `mathlib.c` | s, c, a, l, e, j |
| `main.c` | options, signals, big-stack thread, messages |
| `tests/` | the test suites |
| `test_bc.sh` | runs the tests: `make test`, or `sh test_bc.sh` |
| `Makefile` | build, test, difftest, install, clean |
