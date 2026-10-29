# expr — evaluate arguments as an expression (POSIX.1-2024)

An implementation of the `expr` utility of POSIX.1-2024 (XCU expr), in C
with no dependencies beyond libc.

    expr operand...

## Build and test

    make           # builds ./expr
    make test      # the tests (sh test_expr.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* The operators of the standard with their precedence and left
  associativity; integers in intmax_t with overflow detected; : with basic
  regular expressions anchored at the start, giving the length matched (in
  characters) or \1.
* Exit statuses 0, 1, 2 and 3 as the standard says.

## Choices where the standard leaves room

* A first "--" is discarded.
* An operator symbol where an operand is expected is an operand (expr + : +
  is 1).
* Division by zero, overflow and non-integer arithmetic are expression
  errors (status 2); GNU expr has no overflow (it uses big numbers).
* length, substr, index and match are ordinary strings.

## Testing

`test_expr.sh` runs the utility on prepared input and compares the output
and the exit status: every operator, precedence and parentheses, integer and
string comparison, | and & with null and zero values, : with and without
subexpressions, errors and overflow, characters in UTF-8 (the same results
as GNU expr but for the choices above). The binary tested is `./expr`, or
`$U` if set.

## Files

| file | contents |
|------|----------|
| `expr.c` | the utility |
| `test_expr.sh` | the tests: `make test`, or `sh test_expr.sh` |
| `Makefile` | build, test, install, clean |
