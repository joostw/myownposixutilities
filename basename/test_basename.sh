#!/bin/sh
# test_basename.sh - tests for basename (POSIX.1-2024).  Run: sh test_basename.sh
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
U=${U:-$PWD/basename}
t 'plain' 'b' 0 $U /a/b
t 'trailing slashes' 'b' 0 $U /a/b///
t 'only slashes' '/' 0 $U ///
t 'two slashes' '/' 0 $U //
t 'no slash' 'file' 0 $U file
t 'empty string' '.' 0 $U ''
t 'suffix' 'b' 0 $U /a/b.c .c
t 'suffix equal to name kept' '.c' 0 $U /a/.c .c
t 'suffix not matching' 'b.c' 0 $U b.c .h
t 'suffix after trailing slash' 'b' 0 $U a/b.c/ .c
t 'empty suffix' 'b.c' 0 $U b.c ''
t '-- discarded' 'b' 0 $U -- /a/b
t '-- then suffix' 'b' 0 $U -- b.c .c
t '-- as the string' '--' 0 $U -- --
t 'string starting with -' '-x' 0 $U -x
t 'no operand' '' 1 $U
t 'too many operands' '' 1 $U a b c
[ -c /dev/full ] && t 'write error' '' 1 sh -c "$U a >/dev/full"
done_tests basename
