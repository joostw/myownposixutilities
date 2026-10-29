#!/bin/sh
# test_unexpand.sh - tests for unexpand (POSIX.1-2024).  Run: sh test_unexpand.sh
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
U=${U:-$PWD/unexpand}
T=${TMPDIR:-/tmp}/test_unexpand.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
x() { _n=$1 _i=$2 _e=$3; shift 3; t "$_n" "$_e" 0 sh -c 'printf "$1" | { shift; "$0" "$@"; } | tr "\t " "T."' "$U" "$_i" "$@"; }
x "leading spaces" '        x\n' "Tx"
x "leading spaces and more" '           x\n' "T...x"
x "leading tabs and spaces" '  \t  x\n' "T..x"
x "not leading: unchanged" 'a        b\n' "a........b"
x "-a: two or more before a stop" 'abcdef  x\n' "abcdefTx" -a
x "-a: one before a stop stays" 'abcdefg x\n' "abcdefg.x" -a
x "-a: a run past a stop" 'abcd         x\n' "abcdT.....x" -a
x "-a: a run over two stops" 'abcd             x\n' "abcdTT.x" -a
x "-t: like -a" 'ab  cd\n' "abTcd" -t 4
x "-t a list" '     x  y\n' "T..xTy" -t 3,8
x "-t past the list: unchanged" '              x\n' "TT........x" -t 3,6
x "a single leading space" ' x\n' ".x"
x "spaces that do not reach a stop" '   x\n' "...x"
x "a backspace ends the start" 'a\b        x\n' "$(printf 'a\b........x')"
x "a backspace counts back, and ends the start" '   \b     x\n' "$(printf '...\b.....x')"
x "trailing blanks" 'x        \n' "x........"
x "-a trailing blanks" 'x        \n' "xT." -a
x "no newline at the end" '        x' "Tx"
t "a missing file" "" 1 "$U" nofile
t "a bad list" "" 1 "$U" -t 3,2
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: unexpand f -a: -a is a file" "Tx" 1 sh -c 'printf "        x\n" > f; "$1" f -a > out; r=$?; tr "\t" T < out; exit $r' sh "$U"

done_tests unexpand
