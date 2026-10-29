#!/bin/sh
# test_csplit.sh - tests for csplit (POSIX.1-2024).  Run: sh test_csplit.sh
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
U=${U:-$PWD/csplit}
T=${TMPDIR:-/tmp}/test_csplit.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
awk 'BEGIN { for (i = 1; i <= 30; i++) print i }' > c30
printf 'a\nb\n--\nc\nd\n--\ne\nlast no newline' > cd
# cs ARGS...: csplit in a new directory; the sizes it writes, then the files
cs() { n=0; while [ -e r$n ]; do n=$((n+1)); done; mkdir r$n && cd r$n && "$U" "$@"; _r=$?; for f in *; do [ -f "$f" ] && printf '%s:%s\n' "$f" "$(tr '\n' ' ' < "$f")"; done; cd "$T"; return $_r; }
t "a line number" "6${nl}75${nl}xx00:1 2 3 ${nl}xx01:4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 " 0 cs ../c30 4
t "two line numbers" "6${nl}15${nl}60" 0 sh -c 'mkdir a1 && cd a1 && "$1" ../c30 4 11' sh "$U"
t "a line number of the current line: an empty piece" "0${nl}81" 0 sh -c 'mkdir a2 && cd a2 && "$1" ../c30 1' sh "$U"
t "/rexp/: up to the matching line" "xx00:a b ${nl}xx01:-- c d -- e last no newline" 0 cs -s ../cd /--/
t "/rexp/ twice: the next match" "xx00:a b ${nl}xx01:-- c d ${nl}xx02:-- e last no newline" 0 cs -s ../cd /--/ /--/
t "/rexp/ {1}: the same" "xx00:a b ${nl}xx01:-- c d ${nl}xx02:-- e last no newline" 0 cs -s ../cd /--/ "{1}"
t "/rexp/+1: the line after" "xx00:a b -- ${nl}xx01:c d -- e last no newline" 0 cs -s ../cd /--/+1
t "/rexp/-1" "xx00:a ${nl}xx01:b -- c d -- e last no newline" 0 cs -s ../cd /--/-1
t "%rexp%: skipped" "xx00:-- c d ${nl}xx01:-- e last no newline" 0 cs -s ../cd %--% /--/
t "%rexp%+1, then a line number" "xx00:c d -- ${nl}xx01:e last no newline" 0 cs -s ../cd %--%+1 7
t "the first search starts at the first line" "xx00:${nl}xx01:a b -- c d -- e last no newline" 0 cs -s ../cd /a/
t "a line number {2}: every so many lines" "xx00:1 2 3 4 ${nl}xx01:5 6 7 8 9 ${nl}xx02:10 11 12 13 14 ${nl}xx03:15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 " 0 cs -s ../c30 5 "{2}"
t "a \\/ in the expression" "xx00:x ${nl}xx01:a/b y " 0 sh -c 'printf "x\na/b\ny\n" > s && mkdir b1 && cd b1 && "$1" -s ../s "/a\/b/" && for f in *; do printf "%s:%s\n" "$f" "$(tr "\n" " " < $f)"; done' sh "$U"
t "a \\% in %rexp%" "xx00:a%b y " 0 sh -c 'printf "x\na%%b\ny\n" > s2 && mkdir b2 && cd b2 && "$1" -s ../s2 "%a\%b%" && for f in *; do printf "%s:%s\n" "$f" "$(tr "\n" " " < $f)"; done' sh "$U"
t "-f prefix" "p00 p01" 0 sh -c 'mkdir b3 && cd b3 && "$1" -s -f p ../c30 5 && echo $(ls)' sh "$U"
t "-n 3" "xx000 xx001" 0 sh -c 'mkdir b4 && cd b4 && "$1" -s -n 3 ../c30 5 && echo $(ls)' sh "$U"
t "-s: no sizes" "" 0 sh -c 'mkdir b5 && cd b5 && "$1" -s ../c30 5' sh "$U"
t "the standard input" "6${nl}75" 0 sh -c 'mkdir b6 && cd b6 && "$1" - 4 < ../c30' sh "$U"
t "the last line without a newline is kept" "last no newline" 0 sh -c 'mkdir b7 && cd b7 && "$1" -s ../cd 7 && cat xx01 | tail -n 1' sh "$U"
# errors: the pieces made are removed, unless -k
t "no match: an error" "" 1 sh -c 'mkdir e1 && cd e1 && "$1" ../c30 5 /nomatch/ >/dev/null' sh "$U"
t "no match: the pieces are removed" "" 0 sh -c 'ls e1'
t "-k: the pieces are kept" "xx00" 0 sh -c 'mkdir e2 && cd e2 && "$1" -k ../c30 5 /nomatch/ >/dev/null 2>&1; ls' sh "$U"
t "a line before the current one" "" 1 sh -c 'mkdir e3 && cd e3 && "$1" ../c30 10 5 >/dev/null' sh "$U"
t "a line past the end" "" 1 sh -c 'mkdir e4 && cd e4 && "$1" ../c30 31' sh "$U"
t "a repeat past the end" "" 1 sh -c 'mkdir e5 && cd e5 && "$1" ../c30 10 "{5}" >/dev/null' sh "$U"
t "more pieces than -n 1 names" "" 1 sh -c 'mkdir e6 && cd e6 && "$1" -n 1 ../c30 2 "{10}" >/dev/null' sh "$U"
t "a name too long: nothing made" "" 1 sh -c 'mkdir e7 && cd e7 && "$1" -f $(awk "BEGIN { while (i++ < 254) printf \"p\" }") ../c30 5; r=$?; ls | grep . && exit 9; exit $r' sh "$U"
t "a missing file" "" 1 "$U" no_such_file 5
t "a regular expression that is not valid" "" 2 "$U" c30 "/a\\(/"
t "not an argument" "" 2 "$U" c30 x5
t "line number 0" "" 2 "$U" c30 0
t "a repeat first" "" 2 "$U" c30 "{3}"
t "a repeat that is not valid" "" 2 "$U" c30 5 "{x}"
t "-n 0" "" 2 "$U" -n 0 c30 5
t "no arg" "" 2 "$U" c30
t "an unknown option" "" 2 "$U" -x c30 5
# as the system's csplit makes them
for a in "5" "5 10" "5 5" "/5/" "/5/+2" "/5/-2" "%5%" "%5% /9/" "/1/ {3}" "/[0-9]/ {3}" "10 {1}" "5 {4}" "/2\$/ {2}" "30" "/30/+1" "%3%+1 7"; do
    t "as the system's csplit: $a" "same" 0 sh -c 'rm -rf A B && mkdir A B && (cd A && "$1" ../c30 $2 > out) && (cd B && csplit ../c30 $2 > out) && for f in A/*; do cmp -s "$f" "B/${f#A/}" || exit 1; done && [ "$(ls A)" = "$(ls B)" ] && echo same' sh "$U" "$a"
done
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: csplit c30 -k 5: -k is an argument" "" 2 sh -c 'mkdir g1 && cd g1 && "$1" ../c30 -k 5' sh "$U"

done_tests csplit
