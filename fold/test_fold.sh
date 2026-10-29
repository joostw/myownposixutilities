#!/bin/sh
# test_fold.sh - tests for fold (POSIX.1-2024).  Run: sh test_fold.sh
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
U=${U:-$PWD/fold}
T=${TMPDIR:-/tmp}/test_fold.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# a UTF-8 locale for the cases with characters outside ASCII (POSIX
# names none): $UTF8_LOCALE, or C.UTF-8, or any one that locale -a lists
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
u8() { [ -n "$U8" ]; }
# f NAME INPUT EXPECTED OPTION...: printf INPUT | fold OPTION...
f() { _n=$1 _i=$2 _e=$3; shift 3; t "$_n" "$_e" 0 sh -c 'printf "$1" | { shift; "$0" "$@"; }' "$U" "$_i" "$@"; }
f "short lines pass" 'abc\nde\n' "abc${nl}de" -w 5
f "-w" 'abcdefghij\n' "abcd${nl}efgh${nl}ij" -w 4
f "the default of 80" "$(awk 'BEGIN { while (i++ < 85) printf "x" }')\n" "$(awk 'BEGIN { while (i++ < 80) printf "x" }')${nl}xxxxx"
f "exactly the width" 'abcd\n' "abcd" -w 4
f "-s at a blank" 'abc def ghi\n' "abc ${nl}def ${nl}ghi" -s -w 5
f "-s without a blank" 'abcdefgh\n' "abcde${nl}fgh" -s -w 5
f "-s the last blank" 'a b cdef\n' "a b ${nl}cdef" -s -w 5
f "a tab goes to the next stop" 'ab\tcd\n' "ab	c${nl}d" -w 9
f "a tab counts its columns" 'a\tb\n' "a${nl}	${nl}b" -w 7
f "-b: a tab is one byte" 'a\tbc\n' "a	b${nl}c" -b -w 3
f "a backspace goes back" 'abc\bde\n' "$(printf 'abc\bd\ne')" -w 3
f "a carriage return starts again" 'abc\rdefgh\n' "$(printf 'abc\rdef\ngh')" -w 3
f "-b counts them as bytes" 'ab\bcd\n' "$(printf 'ab\b\ncd')" -b -w 3
f "no newline at the end" 'abcdef' "abc${nl}def" -w 3
f "empty lines" 'a\n\n\nb\n' "a${nl}${nl}${nl}b" -w 3
f "-w 1" 'abc\n' "a${nl}b${nl}c" -w 1
u8 && t "UTF-8: characters are not split" "éé${nl}é" 0 sh -c 'printf "ééé\n" | LC_ALL=$2 "$1" -w 2' sh "$U" "$U8"
u8 && t "UTF-8: wide characters count two columns" "日${nl}本" 0 sh -c 'printf "日本\n" | LC_ALL=$2 "$1" -w 3' sh "$U" "$U8"
u8 && t "UTF-8: -b counts bytes, characters whole" "é${nl}é" 0 sh -c 'printf "éé\n" | LC_ALL=$2 "$1" -b -w 3' sh "$U" "$U8"
t "files" "ab${nl}c${nl}de${nl}f" 0 sh -c 'printf "abc\n" > a; printf "def\n" > b; "$1" -w 2 a b' sh "$U"
t "a missing file" "" 1 "$U" nofile
t "-w 0" "" 1 "$U" -w 0
t "-w x" "" 1 "$U" -w x
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: fold a -b: -b is a file" "abc" 1 "$U" a -b

done_tests fold
