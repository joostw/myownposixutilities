#!/bin/sh
# test_expand.sh - tests for expand (POSIX.1-2024).  Run: sh test_expand.sh
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
U=${U:-$PWD/expand}
T=${TMPDIR:-/tmp}/test_expand.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
e() { _n=$1 _i=$2 _e=$3; shift 3; t "$_n" "$_e" 0 sh -c 'printf "$1" | { shift; "$0" "$@"; } | tr " " .' "$U" "$_i" "$@"; }
e "a tab" 'a\tb\n' "a.......b"
e "tabs" '\t\tx\n' "................x"
e "at a stop" '12345678\tx\n' "12345678........x"
e "-t 4" 'a\tbc\td\n' "a...bc..d" -t 4
e "-t a list" 'a\tb\tc\td\n' "a..b....c.d" -t 3,8,10
e "-t a list with blanks" 'a\tb\tc\n' "a..b....c" -t '3 8'
e "past the list: one space" 'a\tb\tc\td\n' "a..b.c.d" -t 3,4
e "-t 1" 'a\tb\n' "a.b" -t 1
e "a backspace goes back" 'abc\b\tx\n' "$(printf 'abc\b..x')" -t 4
e "a backspace at the start" '\b\tx\n' "$(printf '\b....x')" -t 4
e "spaces stay" 'a  b\n' "a..b"
e "no newline at the end" 'a\tb' "a.......b"
t "files" "a.......b${nl}c.......d" 0 sh -c 'printf "a\tb\n" > f1; printf "c\td\n" > f2; "$1" f1 f2 | tr " " .' sh "$U"
t "a list not ascending" "" 1 "$U" -t 4,3 f1
t "a stop of 0" "" 1 "$U" -t 0 f1
t "a bad list" "" 1 "$U" -t 4x f1
t "a missing file" "" 1 "$U" nofile
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: expand f1 -t: -t is a file" "a.......b" 1 sh -c '"$1" f1 -t > out; r=$?; tr " " . < out; exit $r' sh "$U"

done_tests expand
