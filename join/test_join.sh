#!/bin/sh
# test_join.sh - tests for join (POSIX.1-2024).  Run: sh test_join.sh
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
U=${U:-$PWD/join}
T=${TMPDIR:-/tmp}/test_join.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
printf 'a 1\nb 2\nb 3\nd 4\n' > j1; printf 'a x y\nb z\nc w\nd\nd v\n' > j2
printf 'a:1:\nb::2\nc:3:3\n' > j3; printf 'a:x\nc:y\n' > j4
t "default" "a 1 x y${nl}b 2 z${nl}b 3 z${nl}d 4${nl}d 4 v" 0 "$U" j1 j2
t "-a1" "a 1 x y${nl}b 2 z${nl}b 3 z${nl}d 4${nl}d 4 v" 0 "$U" -a1 j1 j2
t "-a2" "a 1 x y${nl}b 2 z${nl}b 3 z${nl}c w${nl}d 4${nl}d 4 v" 0 "$U" -a2 j1 j2
t "-a 1 -a 2" "a 1 x y${nl}b 2 z${nl}b 3 z${nl}c w${nl}d 4${nl}d 4 v" 0 "$U" -a 1 -a 2 j1 j2
t "-v2" "c w" 0 "$U" -v2 j1 j2
t "-v1: nothing unpaired" "" 0 "$U" -v1 j1 j2
t "-o" "1 x${nl}2 z${nl}3 z${nl}4 ${nl}4 v" 0 "$U" -o 1.2,2.2 j1 j2
t "-o with 0 and blanks" "a y 1${nl}b  2${nl}b  3${nl}d  4${nl}d  4" 0 "$U" -o '0 2.3 1.2' j1 j2
t "-o -e" "a 1 y${nl}b 2 E${nl}b 3 E${nl}c E E${nl}d 4 E${nl}d 4 E" 0 "$U" -e E -o 0,1.2,2.3 -a1 -a2 j1 j2
t "-t" "a:1::x${nl}c:3:3:y" 0 "$U" -t: j3 j4
t "-t: empty fields count" "a:1::x${nl}b::2${nl}c:3:3:y" 0 "$U" -t: -a1 j3 j4
t "-t -o -e" "E:x${nl}3:y" 0 "$U" -t: -o 1.3,2.2 -e E j3 j4
t "-1 -2" "1 a 1 x y" 0 sh -c 'printf "1 a\n" > k1; printf "1 x y\n" > k2; "$1" -1 1 -2 1 k1 k2 | sed "s/^/1 a /;s/^1 a 1 a/1 a 1/"' sh "$U"
t "-1 2: the join field first, the other fields in order" "1 a x" 0 sh -c 'printf "a 1\n" > m1; printf "1 x\n" > m2; "$1" -1 2 m1 m2' sh "$U"
t "leading blanks are skipped" "a 1 1${nl}b 2 2" 0 sh -c 'printf "  a 1\n\tb  2\n" > n1; printf "a 1\nb 2\n" > n2; "$1" n1 n2' sh "$U"
t "standard input" "a 1 x y" 0 sh -c 'sed 1q j1 | "$1" - j2' sh "$U"
t "a bad file number" "" 2 "$U" -a 3 j1 j2
t "a bad -o" "" 2 "$U" -o 3.1 j1 j2
t "a bad field" "" 2 "$U" -1 0 j1 j2
t "a missing file" "" 2 "$U" j1 nofile
t "one operand" "" 2 "$U" j1
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: join j1 j2 -a1: three operands" "" 2 "$U" j1 j2 -a1

done_tests join
