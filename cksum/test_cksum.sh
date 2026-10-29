#!/bin/sh
# test_cksum.sh - tests for cksum (POSIX.1-2024).  Run: sh test_cksum.sh
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
U=${U:-$PWD/cksum}
T=${TMPDIR:-/tmp}/test_cksum.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# the values agree with GNU cksum and with the algorithm of the standard
t "empty" "4294967295 0" 0 sh -c ': | "$1"' sh "$U"
t "abc" "1219131554 3" 0 sh -c 'printf abc | "$1"' sh "$U"
t "a line" "3015617425 6" 0 sh -c 'echo hello | "$1"' sh "$U"
t "a file" "3015617425 6 f" 0 sh -c 'echo hello > f && "$1" f' sh "$U"
t "files" "3015617425 6 f${nl}4294967295 0 e" 0 sh -c ': > e && "$1" f e' sh "$U"
t "binary data" "1313719201 256" 0 sh -c 'awk "BEGIN { for (i = 0; i < 256; i++) printf \"%c\", i }" > b; LC_ALL=C "$1" < b' sh "$U"
awk 'BEGIN { srand(1); for (i = 0; i < 300000; i++) printf "%d\n", int(rand() * 1000) }' > big
t "a large file: its size" "$(wc -c < big | tr -d ' ')" 0 sh -c '"$1" big | cut -d " " -f 2' sh "$U"
t "a missing file: the others" "4294967295 0 e" 1 "$U" nofile e
t "--" "4294967295 0 e" 0 "$U" -- e
[ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" e >/dev/full' sh "$U"

done_tests cksum
