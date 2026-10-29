# awk — pattern scanning and processing language (POSIX.1-2024)

An implementation of the `awk` utility following IEEE Std 1003.1-2024
(The Open Group Base Specifications Issue 8). It is written in C11 and uses
only POSIX interfaces (`regcomp`/`regexec`, `popen`, `read`, locale functions).
The grammar is a yacc grammar (`awk.y`) and the tokens a lex scanner
(`awk.l`), both generated with the `yacc` and `lex` of this project; the
generated files are included, so building needs only a C compiler.

## Build and test

    make           # builds ./awk
    make test      # the tests (sh test_awk.sh): regression suite
    make difftest  # compares with gawk --posix (needs gawk)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

To regenerate the parser and scanner, delete the generated files and run
`make`; it uses `yacc` and `lex` from PATH, or `make YACC=... LEX=...` (for
instance the ones in `../yacc` and `../lex`).

## Where yacc and lex are used

* **The grammar** (`awk.y`) has the levels of the precedence table, with
  the choices of the earlier hand-written parser kept: `!` takes a unary
  operand, `^` a unary right operand (`2^-1`), concatenation an operand
  that does not start with `+` or `-`, and comparisons do not associate.
  In the expression list of `print` and `printf`, `>` is a redirection
  outside parentheses, so that part of the grammar comes twice (the `p*`
  rules). Error rules give the earlier messages (`expected )`, `statement
  not terminated`, `unexpected token after pattern or action`, ...), each
  placed after the token that expects something. The 36 shift/reduce
  conflicts are all resolved by shifting, as the earlier parser did: the
  dangling `else`, postfix `++`, `getline var` and `getline < expr`, and
  newlines that belong to the statement before them.
* **The scanner** (`awk.l`) decides whether `/` starts a regular
  expression from the previous token (a `)` that closes `if (`, `while (`
  or `for (` does not end an operand, nor do `++`/`--` after `$`). Where
  awk's rules depend on what follows a token, it looks at the text rather
  than at further tokens: `name(` is a function call, and an item that
  ends in `}` may be followed directly by another (the scanner then gives
  the grammar a separator). Inside a block it gives the grammar a
  terminator before `}` and at the end of the program, where a statement
  may end without `;` or a newline. Whether `|` is followed by `getline`
  is asked for by the grammar at the end of each part of a
  concatenation, where the earlier parser looked ahead.

## What is implemented

* Full grammar: patterns, ranges, `BEGIN`/`END`, functions (with recursion,
  array parameters by reference, untyped locals that become arrays), all
  statements including `nextfile`, `delete array`, `do…while`, `for (k in a)`.
* Lexical rules: `/` division vs. ERE resolved by syntactic context; newline
  rules after `,` `{` `&&` `||` `do` `else` and `)`; backslash-newline;
  `FUNC_NAME` requires `(` with no space.
* Value model: uninitialized values, numeric strings (fields, getline, `FILENAME`,
  `ARGV`, `ENVIRON`, `split`, command-line assignments, propagation through
  assignment), the four numeric-comparison rules, `==`/`!=` by identity and
  `<` etc. by `strcoll`. Numbers convert via `CONVFMT` (internal) and `OFMT`
  (output); integral values use `%d` at any magnitude.
* String→number uses the decimal form described in the spec (no hex/inf/nan).
* Fields: lazy splitting with the FS current when the record was read; default
  whitespace, single-character, and ERE separators (empty matches never
  delimit); `$(NF+2)=x`, `NF` assignment, `$0` recomputation with `OFS`.
* `RS`: single character, and `RS=""` paragraph mode where newline is always a
  field separator.
* EREs with awk escapes (`\/`, `\"`, `\ddd`, `\n`…, `\.` etc.), inside and
  outside bracket expressions; dynamic regexes from strings are cached.
* All built-ins, including `fflush`, `length(array)`; `index`, `length`,
  `match`, `substr`, `toupper`/`tolower`, `%c` and `%s` width/precision work
  in characters in multibyte locales.
* I/O: `>`, `>>`, `|` redirections, `close` (returns the command's exit status
  for pipes), all `getline` forms with the correct `NR`/`FNR`/`NF` updates,
  `system`. Output is flushed before running commands so ordering is preserved.
  `/dev/stdout`, `/dev/stderr` and `-` are handled specially.
* `ARGV`/`ARGC` processed dynamically as the spec's iteration describes,
  including assignment operands, deleted/empty elements, and `-`.
* `exit` runs `END` actions (also from `BEGIN`); exit status semantics.
* Unreadable file operands are diagnosed and awk exits with status 2.

## Where it follows the spec rather than common practice

Differential testing against gawk, onetrue awk (BWK) and mawk turned up a few
places where every historical implementation departs from the 2024 text.
This implementation follows the text:

* **Empty fields are uninitialized**, so with `FS="\t"` and input `\t\tz`,
  `$1 == 0` is true (numeric comparison with the uninitialized value). Other
  awks treat the empty field as a string.
* **`RS=""` and blank lines.** Records are separated by a newline plus one or
  more *blank lines* (XBD: lines of zero or more `<blank>`s), so a line holding
  only spaces separates paragraphs. Others recognize only empty lines.
* **`sub`/`gsub` replacement:** `\\` always yields one literal backslash (the
  text added by PASC Interpretation #211), not only before `&`.
* **`FILENAME` is `-`** when reading standard input because no file operands
  were given.

## Choices where the standard leaves room

* `substr(s, m, n)` with non-integer or `m < 1`: truncate, clamp `m` to 1,
  keep `n` (BWK/gawk practice).
* Multi-character `RS` is treated as an ERE (as gawk does).
* `FS=""` or `split(s, a, "")` splits into characters.
* Unknown string escapes such as `"\q"` keep the backslash (so `"\."` works as
  a dynamic regex for a literal dot).
* `cmd | getline` sets `$0` and `NF` only; it does not change `NR` (the text
  names only `$0` and `NF`).
* Division or modulus by zero is a fatal error (exit 2).
* `**` and `**=` are accepted as synonyms for `^` and `^=`.
* `srand()` returns the previous seed; the initial seed is 0; `rand()` uses
  `drand48`.

## Testing

`tests/run.sh` checks 331 cases against frozen expected outputs. `tests/diff.sh`
runs the same cases against another implementation
(`REF="gawk" ./diff.sh`, `CASES=cases2.sh ...`); the only remaining differences
against gawk are the spec-mandated items above or behaviour the spec leaves
undefined. The suite also runs clean under AddressSanitizer and UBSan.

The move to yacc and lex was checked against the previous release (the
hand-written parser), comparing standard output, standard error and exit
status byte for byte (`tests/equiv.py OLD NEW mode ...`):

* `ast`: random programs covering every construct, with the parsed
  program printed by a test-only dumper (`tests/astdump.c`, linked into
  both builds) and compared node for node, including the order in which
  variables and functions are created;
* `mut`: the random programs with tokens deleted, repeated, swapped or
  inserted, so that syntax errors land everywhere; every message the
  parser has turns up, and each matches, context line included;
* `grid`: 48,160 variants of 17 templates, every token replaced by and
  preceded by each of 69 tokens (keywords, operators, invalid
  characters, unterminated strings and regular expressions);
* `edge`: hand-written cases (NUL bytes, carriage returns, programs that
  end inside a token, continuations, the corners of bracket expressions,
  names with letters outside ASCII), each in the C locale, a UTF-8 locale
  (where there is one) and a Latin-1 locale;
* `cases` and `run`: the regression cases and random programs run on
  input.

All of these, about 65,000 comparisons, are identical, and nesting a
million levels deep behaves the same as before. The parser's error paths
were also fuzzed under AddressSanitizer and UBSan, and a build whose
parser and scanner are regenerated with bison and flex passes the
regression suite and gives the same error messages.

## Files

| file      | contents |
|-----------|----------|
| `awk.h`   | shared types: refcounted strings, values, arrays, AST |
| `lib.c`   | strings, hash arrays, number⇄string conversion, the printf engine, awk-ERE → POSIX-ERE translation and regex cache, multibyte helpers |
| `awk.l`   | the tokens (`lex.c` generated): context-sensitive `/` vs ERE, strings, numbers, error context |
| `awk.y`   | the grammar and precedence table, building the syntax tree (`awk.tab.c`, `awk.tab.h` generated) |
| `parse.h` | the interface between the scanner, the grammar and `parse.c` |
| `parse.c` | symbol tables, keywords and built-in names, the program's rules and actions |
| `run.c`   | tree-walking interpreter: fields/records, RS handling, ARGV processing, I/O streams, getline, builtins, user functions |
| `main.c`  | option parsing (`-F`, `-f`, `-v`, `--`) per the Utility Syntax Guidelines |
| `tests/`  | `run.sh` regression suite, `diff.sh` differential runner against another awk, `equiv.py` equivalence tests between two builds |
| `test_awk.sh` | runs the tests: `make test`, or `sh test_awk.sh` |
| `Makefile` | build, test, difftest, install, clean |
