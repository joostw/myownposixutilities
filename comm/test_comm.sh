#!/bin/sh
# test_comm.sh - tests for comm (POSIX.1-2024).  Run: sh test_comm.sh
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
U=${U:-$PWD/comm}
T=${TMPDIR:-/tmp}/test_comm.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
printf 'a\nb\nd\n' > f1; printf 'b\nc\nd\ne\n' > f2
t "three columns" "a${nl}		b${nl}	c${nl}		d${nl}	e" 0 "$U" f1 f2
t "-1" "	b${nl}c${nl}	d${nl}e" 0 "$U" -1 f1 f2
t "-2" "a${nl}	b${nl}	d" 0 "$U" -2 f1 f2
t "-3" "a${nl}	c${nl}	e" 0 "$U" -3 f1 f2
t "-12" "b${nl}d" 0 "$U" -12 f1 f2
t "-13" "c${nl}e" 0 "$U" -13 f1 f2
t "-23" "a" 0 "$U" -23 f1 f2
t "-123" "" 0 "$U" -123 f1 f2
t "-1 -2 as separate options" "b${nl}d" 0 "$U" -1 -2 f1 f2
t "an empty file" "a${nl}b${nl}d" 0 sh -c ': > e; "$1" f1 e' sh "$U"
t "standard input" "		a${nl}		b${nl}		d" 0 sh -c '"$1" - f1 < f1' sh "$U"
t "no newline at the end" "		a${nl}		b" 0 sh -c 'printf "a\nb" > g1; printf "a\nb\n" > g2; "$1" g1 g2' sh "$U"
t "duplicate lines" "		a${nl}a${nl}		b" 0 sh -c 'printf "a\na\nb\n" > h1; printf "a\nb\n" > h2; "$1" h1 h2' sh "$U"
t "lines are compared in full" "ab${nl}	abc" 0 sh -c 'printf "ab\n" > k1; printf "abc\n" > k2; "$1" k1 k2' sh "$U"
t "a missing file" "" 2 "$U" f1 nofile
t "one operand" "" 2 "$U" f1
t "a bad option" "" 2 "$U" -4 f1 f2
[ -c /dev/full ] && t "a write error" "" 2 sh -c '"$1" f1 f2 >/dev/full' sh "$U"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: comm f1 f2 -1: three operands" "" 2 "$U" f1 f2 -1

done_tests comm
