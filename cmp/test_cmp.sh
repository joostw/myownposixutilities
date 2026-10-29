#!/bin/sh
# test_cmp.sh - tests for cmp (POSIX.1-2024).  Run: sh test_cmp.sh
cd "$(dirname "$0")" || exit 2
pass=0 fail=0
# t NAME EXPECTED-STDOUT EXPECTED-STATUS COMMAND...  (stdout compared without trailing newlines)
t() {
    _n=$1 _e=$2 _s=$3; shift 3
    _o=$("$@" 2>/dev/null); _r=$?
    if [ "$_o" = "$_e" ] && [ "$_r" = "$_s" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s] status %s\n  want: [%s] status %s\n' "$_n" "$_o" "$_r" "$_e" "$_s"; fi
}
# te NAME EXPECTED-STDERR COMMAND...  (standard error)
te() {
    _n=$1 _e=$2; shift 2
    _o=$("$@" 2>&1 >/dev/null)
    if [ "$_o" = "$_e" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s]\n  want: [%s]\n' "$_n" "$_o" "$_e"; fi
}
done_tests() { echo "$1: $pass passed, $fail failed"; [ "$fail" = 0 ]; }
U=${U:-$PWD/cmp}
T=${TMPDIR:-/tmp}/test_cmp.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C

printf 'abc\ndef\nghi\n' > a
printf 'abc\ndef\nghi\n' > a2
printf 'abc\ndxf\ngHi\n' > b
printf 'abc\nde' > short
printf 'abc\n' > line
: > empty
t "identical" "" 0 "$U" a a2
t "the same file" "" 0 "$U" a a
t "different" "a b differ: char 6, line 2" 1 "$U" a b
t "the first byte" "a x differ: char 1, line 1" 1 sh -c 'echo X > x; "$1" a x' sh "$U"
t "-l" "6 145 170
10 150 110" 1 "$U" -l a b
t "-s" "" 1 "$U" -s a b
te "-s: nothing on standard error" "" "$U" -s a b
t "-s identical" "" 0 "$U" -s a a2
t "a prefix" "" 1 "$U" a short
te "a prefix: EOF" "cmp: EOF on short after byte 6, line 2" "$U" a short
te "a prefix, the other way" "cmp: EOF on short after byte 6, line 2" "$U" short a
te "a prefix ending in a newline" "cmp: EOF on line after byte 4, line 1" "$U" line a
te "an empty file" "cmp: EOF on empty (empty)" "$U" empty a
t "an empty file: status" "" 1 "$U" empty a
t "two empty files" "" 0 sh -c ': > e2; "$1" empty e2' sh "$U"
t "-l a prefix" "" 1 "$U" -l short a
te "-l a prefix: EOF" "cmp: EOF on short after byte 6, line 2" "$U" -l short a
t "-l with differences and a prefix" "6 170 145" 1 "$U" -l b short
te "-s a prefix: nothing" "" "$U" -s a short
t "standard input" "" 0 sh -c '"$1" - a2 < a' sh "$U"
t "standard input second" "b - differ: char 6, line 2" 1 sh -c '"$1" b - < a' sh "$U"
t "standard input first" "- b differ: char 6, line 2" 1 sh -c '"$1" - b < a' sh "$U"
t "binary bytes" "3 0 377" 1 sh -c 'printf "ab\0" > z1; printf "ab\377" > z2; "$1" -l z1 z2' sh "$U"
awk 'BEGIN { for (i = 0; i < 100000; i++) print i }' > big
awk 'BEGIN { for (i = 0; i < 100000; i++) print (i == 77777 ? "x" : i) }' > big2
t "large files" "big big2 differ: char 455553, line 77778" 1 "$U" big big2
t "large identical" "" 0 sh -c 'cp big big3 && "$1" big big3' sh "$U"
t "a missing file" "" 2 "$U" a nofile
t "a missing file with -s" "" 2 "$U" -s nofile a
t "a directory" "" 2 "$U" a .
t "one operand" "" 2 "$U" a
t "three operands" "" 2 "$U" a a a
t "-l and -s" "" 2 "$U" -l -s a b
t "a bad option" "" 2 "$U" -x a b
t "--" "" 0 sh -c 'cp a ./-f && "$1" -- -f a' sh "$U"
[ -c /dev/full ] && t "a write error" "" 2 sh -c '"$1" a b >/dev/full' sh "$U"

# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: cmp f f -l: three operands" "" 2 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" f f -l; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests cmp
