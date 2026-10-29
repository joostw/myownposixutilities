#!/bin/sh
# test_echo.sh - tests for echo (POSIX.1-2024).  Run: sh test_echo.sh
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
U=${U:-$PWD/echo}
tb 'plain' 'a b \n' $U a b
tb 'no arguments' '\n' $U
tb 'empty argument' '\n' $U ''
tb '-n is an operand (XSI)' '- n a \n' $U -n a
tb '-- is an operand' '- - a \n' $U -- a
tb 'escapes' '\a \b \f \n \r \t \v \ \n' $U '\a\b\f\n\r\t\v\\'
tb '\c stops' 'a b' $U 'ab\cd' e
tb '\0num' 'A 001 \0 \n' $U '\0101\01\0'
tb '\0 with three digits only' 'A 1 \n' $U '\01011'
tb 'unknown escape kept' '\ q \n' $U '\q'
tb 'trailing backslash' 'a \ \n' $U 'a\'
[ -c /dev/full ] && t 'write error' '' 1 sh -c "$U a >/dev/full"
done_tests echo
