#!/bin/sh
# test_tsort.sh - tests for tsort (POSIX.1-2024).  Run: sh test_tsort.sh
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
U=${U:-$PWD/tsort}
T=${TMPDIR:-/tmp}/test_tsort.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# ts NAME INPUT EXPECTED STATUS OPTION...
ts() { _n=$1 _i=$2 _e=$3 _s=$4; shift 4; t "$_n" "$_e" "$_s" sh -c 'printf "$1" | { shift; "$0" "$@"; }' "$U" "$_i" "$@"; }
ts "a chain" 'a b\nb c\nc d\n' "a${nl}b${nl}c${nl}d" 0
ts "pairs on one line" 'c d b c a b' "a${nl}b${nl}c${nl}d" 0
ts "a pair of the same item: present" 'x x\na b\n' "x${nl}a${nl}b" 0
ts "a diamond" 'a b a c b d c d\n' "a${nl}b${nl}c${nl}d" 0
ts "tabs and blank lines separate" 'a\tb\n\n\nb   c\n' "a${nl}b${nl}c" 0
ts "the order of the input among free items" 'z y m n\n' "z${nl}m${nl}y${nl}n" 0
ts "a duplicate pair" 'a b a b\n' "a${nl}b" 0
ts "empty input" '' "" 0
ts "a cycle: written anyway, a warning" 'a b b c c a\n' "b${nl}c${nl}a" 0
t "a cycle: the message names it" "tsort: a cycle: b c a" 0 sh -c 'printf "a b b c c a\n" | "$1" 2>&1 >/dev/null' sh "$U"
ts "-w a cycle: status 1" 'a b b c c a\n' "b${nl}c${nl}a" 1 -w
ts "-w two cycles: status 2" 'a b b a c d d c\n' "b${nl}a${nl}d${nl}c" 2 -w
ts "-w no cycle: 0" 'a b\n' "a${nl}b" 0 -w
ts "a cycle and the rest" 'x a a b b a b c\n' "x${nl}b${nl}a${nl}c" 0
t "an odd number of items" "" 1 sh -c 'printf "a b c\n" | "$1"' sh "$U"
t "a file" "a${nl}b" 0 sh -c 'echo a b > f && "$1" f' sh "$U"
t "a missing file" "" 1 "$U" nofile
t "two files" "" 1 "$U" f f
awk 'BEGIN { for (i = 0; i < 20000; i++) print i, i + 1 }' > chain
t "a long chain" "20001 0 20000" 0 sh -c '"$1" chain > out && echo $(wc -l < out) $(sed -n "1p;\$p" out)' sh "$U"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: tsort f -w: two operands" "" 1 "$U" f -w

done_tests tsort
