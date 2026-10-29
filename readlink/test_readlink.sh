#!/bin/sh
# test_readlink.sh - tests for readlink (POSIX.1-2024).  Run: sh test_readlink.sh
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
U=${U:-$PWD/readlink}
T=${TMPDIR:-/tmp}/test_readlink.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
ln -s target l; ln -s "" e 2>/dev/null; ln -s 'a b/c' sp; : > f
t "a link" "target" 0 "$U" l
t "a link with spaces" "a b/c" 0 "$U" sp
t "-n" "target|" 0 sh -c '"$1" -n l; echo "|"' sh "$U"
t "a dangling link" "target" 0 "$U" l
t "not a link" "" 1 "$U" f
t "missing" "" 1 "$U" nofile
t "a link to a link: one step" "l" 0 sh -c 'ln -s l l2 && "$1" l2' sh "$U"
long=$(awk 'BEGIN { while (i++ < 2000) printf "x/" }')
t "a long link" "2000" 0 sh -c 'ln -s "$2" long && "$1" long | tr -cd / | wc -c | tr -d " "' sh "$U" "$long"
t "no operand" "" 2 "$U"
t "two operands" "" 2 "$U" l l
t "a bad option" "" 2 "$U" -x l
[ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" l >/dev/full' sh "$U"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: readlink l -n: two operands" "" 2 "$U" l -n

done_tests readlink
