#!/bin/sh
# test_paste.sh - tests for paste (POSIX.1-2024).  Run: sh test_paste.sh
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
U=${U:-$PWD/paste}
T=${TMPDIR:-/tmp}/test_paste.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
printf 'a\nb\nc\n' > p1; printf '1\n2\n' > p2; printf 'x\ny\nz\nw\n' > p3; : > pe
t "two files" "a	1${nl}b	2${nl}c	" 0 "$U" p1 p2
t "three files, uneven" "a	1	x${nl}b	2	y${nl}c		z${nl}		w" 0 "$U" p1 p2 p3
t "-d" "a:1:x${nl}b:2:y${nl}c::z${nl}::w" 0 "$U" -d : p1 p2 p3
t "-d a list, used circularly" "a:1,x${nl}b:2,y${nl}c:,z${nl}:,w" 0 "$U" -d :, p1 p2 p3
t "-d reset for each line" "a:1:x" 0 sh -c '"$1" -d : p1 p2 p3 | sed 1q' sh "$U"
t "-d \\n" "a${nl}1${nl}b${nl}2${nl}c" 0 "$U" -d '\n' p1 p2
t "-d \\t" "a	1" 0 sh -c '"$1" -d "\\t" p1 p2 | sed 1q' sh "$U"
t "-d \\\\" "a\\1" 0 sh -c '"$1" -d "\\\\" p1 p2 | sed 1q' sh "$U"
t "-d \\0: no delimiter" "a1x" 0 sh -c '"$1" -d "\\0" p1 p2 p3 | sed 1q' sh "$U"
t "-d with \\0 in a list" "a1-x" 0 sh -c '"$1" -d "\\0-" p1 p2 p3 | sed 1q' sh "$U"
t "-s" "a	b	c${nl}1	2${nl}x	y	z	w" 0 "$U" -s p1 p2 p3
t "-s -d" "a:b,c${nl}x:y,z:w" 0 "$U" -s -d :, p1 p3
t "-s an empty file: an empty line" "${nl}a	b	c" 0 sh -c '"$1" -s pe p1' sh "$U"
t "an empty file among others" "a		1${nl}b		2${nl}c		" 0 "$U" p1 pe p2
t "- twice: lines in turn" "x	y${nl}z	w" 0 sh -c '"$1" - - < p3' sh "$U"
t "- with a file" "a	x	1${nl}b	y	2${nl}c	z	${nl}	w	" 0 sh -c '"$1" p1 - p2 < p3' sh "$U"
t "-s -" "x	y	z	w" 0 sh -c '"$1" -s - < p3' sh "$U"
t "no newline at the end" "n	a${nl}	b${nl}	c" 0 sh -c 'printf n > pn; "$1" pn p1' sh "$U"
# a UTF-8 locale for the case with a character outside ASCII (POSIX names
# none): $UTF8_LOCALE, or C.UTF-8, or any one that locale -a lists
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
[ -n "$U8" ] && t "-d with a multibyte character" "a€1€x" 0 sh -c 'LC_ALL=$2 "$1" -d "€" p1 p2 p3 | sed 1q' sh "$U" "$U8"
t "a missing file" "" 1 "$U" p1 nofile
t "a bad option" "" 1 "$U" -x p1
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: paste p1 -s: -s is a file" "" 1 "$U" p1 -s

done_tests paste
