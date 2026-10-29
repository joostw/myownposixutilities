#!/bin/sh
# test_dirname.sh - tests for dirname (POSIX.1-2024).  Run: sh test_dirname.sh
cd "$(dirname "$0")" || exit 2
pass=0 fail=0
# t NAME EXPECTED-STDOUT EXPECTED-STATUS COMMAND...  (stdout compared without trailing newlines)
t() {
    _n=$1 _e=$2 _s=$3; shift 3
    _o=$("$@" 2>/dev/null); _r=$?
    if [ "$_o" = "$_e" ] && [ "$_r" = "$_s" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s] status %s\n  want: [%s] status %s\n' "$_n" "$_o" "$_r" "$_e" "$_s"; fi
}
# tb NAME EXPECTED-OD-C COMMAND...  (exact bytes of stdout, as od -c)
tb() {
    _n=$1 _e=$2; shift 2
    _o=$("$@" 2>/dev/null | od -An -c | tr -s ' \n' ' ' | sed 's/^ //;s/ $//')
    if [ "$_o" = "$_e" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  %s\n  want: %s\n' "$_n" "$_o" "$_e"; fi
}
# ok NAME CONDITION-COMMAND...
ok() {
    _n=$1; shift
    if "$@"; then pass=$((pass+1)); else fail=$((fail+1)); printf 'FAIL %s\n' "$_n"; fi
}
done_tests() { echo "$1: $pass passed, $fail failed"; [ "$fail" = 0 ]; }
U=${U:-$PWD/dirname}
t 'plain' '/a' 0 $U /a/b
t 'trailing slashes' '/a' 0 $U /a/b///
t 'root child' '/' 0 $U /a
t 'only slashes' '/' 0 $U ///
t 'relative' 'a' 0 $U a/b
t 'no slash' '.' 0 $U file
t 'trailing slash only' '.' 0 $U file/
t 'empty string' '.' 0 $U ''
t 'inner slashes' 'a' 0 $U a//b//
t 'deeper' 'a/b' 0 $U a/b/c
t 'double leading' '/' 0 $U //a
t '-- discarded' '/a' 0 $U -- /a/b
t '-- as the string' '.' 0 $U -- --
t 'no operand' '' 1 $U
t 'too many operands' '' 1 $U a b
[ -c /dev/full ] && t 'write error' '' 1 sh -c "$U a/b >/dev/full"
done_tests dirname
