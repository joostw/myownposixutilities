#!/bin/sh
# test_expr.sh - tests for expr (POSIX.1-2024).  Run: sh test_expr.sh
cd "$(dirname "$0")" || exit 2
pass=0 fail=0
# t NAME EXPECTED-STDOUT EXPECTED-STATUS COMMAND...  (stdout compared without trailing newlines)
t() {
    _n=$1 _e=$2 _s=$3; shift 3
    _o=$("$@" 2>/dev/null); _r=$?
    if [ "$_o" = "$_e" ] && [ "$_r" = "$_s" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s] status %s\n  want: [%s] status %s\n' "$_n" "$_o" "$_r" "$_e" "$_s"; fi
}
# ok NAME CONDITION-COMMAND...
ok() {
    _n=$1; shift
    if "$@"; then pass=$((pass+1)); else fail=$((fail+1)); printf 'FAIL %s\n' "$_n"; fi
}
done_tests() { echo "$1: $pass passed, $fail failed"; [ "$fail" = 0 ]; }
U=${U:-$PWD/expr}
T=${TMPDIR:-/tmp}/test_expr.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# x NAME EXPECTED STATUS ARGUMENT...
x() { _n=$1 _e=$2 _s=$3; shift 3; t "$_n" "$_e" "$_s" "$U" "$@"; }
x "+" "3" 0 1 + 2
x "-" "-3" 0 7 - 10
x "*" "12" 0 3 '*' 4
x "/" "3" 0 7 / 2
x "/ negative" "-3" 0 -7 / 2
x "%" "1" 0 7 % -3
x "precedence" "7" 0 1 + 2 '*' 3
x "parentheses" "9" 0 '(' 1 + 2 ')' '*' 3
x "left associative" "1" 0 7 - 3 - 3
x "= integers" "1" 0 5 = 05
x "= strings" "0" 1 a = b
x "< strings" "1" 0 a '<' b
x "< integers" "0" 1 10 '<' 9
x "> strings by collation" "1" 0 b '>' a
x ">=" "0" 1 abc '>=' abd
x "<=" "1" 0 -1 '<=' 0
x "!=" "1" 0 1 != 2
x "| the first" "3" 0 3 '|' 5
x "| zero: the second" "5" 0 0 '|' 5
x "| null: the second" "x" 0 '' '|' x
x "| both null: 0" "0" 1 '' '|' ''
x "| 00 is zero" "7" 0 00 '|' 7
x "& both" "3" 0 3 '&' 5
x "& a zero" "0" 1 0 '&' 5
x "& a null" "0" 1 3 '&' ''
x ": characters matched" "3" 0 abcde : 'a.c'
x ": anchored at the start" "0" 1 abcde : 'b'
x ": a subexpression" "ab" 0 abcde : '\(a.\)'
x ": a subexpression not matched" "" 1 abcde : 'x\(.\)'
x ": .*" "3" 0 abc : '.*'
x ": an empty string" "0" 1 '' : '.*'
x ": an interval" "2" 0 aaa : 'a\{2\}'
x ": ^ changes nothing" "2" 0 abc : '^ab'
x "a string" "007" 0 007
x "zero" "0" 1 0
x "-0" "-0" 1 -0
x "null" "" 1 ''
x "an operator as an operand" "1" 0 + : +
x "--" "5" 0 -- 5
x "-- as a string after the first" "1" 0 -- -- = --
x "not an integer" "" 2 a + 1
x "division by zero" "" 2 1 / 0
x "remainder by zero" "" 2 1 % 0
x "overflow" "" 2 9223372036854775807 + 1
x "overflow *" "" 2 4611686018427387904 '*' 2
x "a missing )" "" 2 '(' 1
x "a missing operand" "" 2 1 +
x "two operands" "" 2 1 2
x "no operands" "" 2
x "a bad pattern" "" 2 a : '\('
if [ -n "$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)" ]; then
    U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
    [ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
    t "UTF-8: : counts characters" "3" 0 env LC_ALL=$U8 "$U" 'héé' : '.*'
fi
[ -c /dev/full ] && t "a write error" "" 3 sh -c '"$1" 1 >/dev/full' sh "$U"

done_tests expr
