#!/bin/sh
# test_pr.sh - tests for pr (POSIX.1-2024).  Run: sh test_pr.sh
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
U=${U:-$PWD/pr}
T=${TMPDIR:-/tmp}/test_pr.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
export TZ=UTC0
printf 'abcdefghijklmnopqrstuvwxyz\nb\nc\nd\ne\nf\ng\n' > s7
awk 'BEGIN { for (i = 1; i <= 60; i++) print "line " i }' > l60
: > empty
touch -t 202001021530 s7 l60 empty
H='Jan  2 15:30 2020'
# the pages
t "the header: two empty lines, date file Page n, two empty lines" "${nl}${nl}$H s7 Page 1${nl}${nl}${nl}x" 0 sh -c '"$1" s7 | sed 5q; echo x' sh "$U"
t "a page: 66 lines" "66" 0 sh -c '"$1" s7 | wc -l | tr -d " "' sh "$U"
t "the trailer: empty lines to the end of the page" "abcdefghijklmnopqrstuvwxyz${nl}b${nl}c${nl}d${nl}e${nl}f${nl}g" 0 sh -c '"$1" s7 | sed 1,5d | sed "/^\$/d"' sh "$U"
t "56 lines a page" "132${nl}$H l60 Page 2${nl}line 57" 0 sh -c '"$1" l60 > o; wc -l < o | tr -d " "; sed -n 69p o; sed -n 72p o' sh "$U"
t "-h" "$H the header Page 1" 0 sh -c '"$1" -h "the header" s7 | sed -n 3p' sh "$U"
t "the standard input: no name, the time now" "yes" 0 sh -c '"$1" < s7 | sed -n 3p | grep -E "^[A-Z][a-z][a-z] [ 0-9][0-9] [0-9][0-9]:[0-9][0-9] [0-9]{4}  Page 1\$" >/dev/null && echo yes' sh "$U"
t "-l 20: 10 lines of text a page" "$H l60 Page 6${nl}line 60" 0 sh -c '"$1" -l 20 l60 | grep -e "Page 6" -e "line 60"' sh "$U"
t "-l 10: as -t" "abcdefghijklmnopqrstuvwxyz${nl}b${nl}c${nl}d${nl}e${nl}f${nl}g" 0 "$U" -l 10 s7
t "-t: no header and trailer" "abcdefghijklmnopqrstuvwxyz${nl}b${nl}c${nl}d${nl}e${nl}f${nl}g" 0 "$U" -t s7
t "+page: from that page on" "$H l60 Page 2" 0 sh -c '"$1" +2 l60 | sed -n 3p' sh "$U"
t "+page: the pages before not written" "66" 0 sh -c '"$1" +2 l60 | wc -l | tr -d " "' sh "$U"
t "+page past the end: nothing" "" 0 "$U" +3 s7
t "an empty file: nothing" "" 0 "$U" empty
t "two files: pages of each" "$H s7 Page 1${nl}$H l60 Page 1${nl}$H l60 Page 2" 0 sh -c '"$1" s7 l60 | grep Page' sh "$U"
t "-F: the body, then a form feed" "ab${nl}cd${nl}FF" 0 sh -c 'printf "ab\ncd\n" | "$1" -F -l 12 | sed 1,5d | sed "s/$(printf "\\f")/FF/"' sh "$U"
t "-f: as -F (and a pause for a terminal)" "same" 0 sh -c 'a=$("$1" -f s7); b=$("$1" -F s7); [ "$a" = "$b" ] && echo same' sh "$U"
# the text
t "-d: double spacing" "abcdefghijklmnopqrstuvwxyz${nl}${nl}b${nl}${nl}c" 0 sh -c '"$1" -t -d s7 | sed 5q' sh "$U"
t "-d: half as many lines a page" "$H l60 Page 3" 0 sh -c '"$1" -d l60 | sed -n 135p' sh "$U"
t "-n: five digits and a tab" "    1	abcdefghijklmnopqrstuvwxyz${nl}    2	b" 0 sh -c '"$1" -t -n s7 | sed 2q' sh "$U"
t "-n:3" "  1:abcdefghijklmnopqrstuvwxyz${nl}  2:b" 0 sh -c '"$1" -t -n:3 s7 | sed 2q' sh "$U"
t "-n3: the width only" "  1	abcdefghijklmnopqrstuvwxyz" 0 sh -c '"$1" -t -n3 s7 | sed 1q' sh "$U"
t "-n: the numbers go on over the pages" "   57	line 57" 0 sh -c '"$1" -n l60 | grep "line 57\$"' sh "$U"
t "-o 2" "  abcdefghijklmnopqrstuvwxyz${nl}  b" 0 sh -c '"$1" -t -o 2 s7 | sed 2q' sh "$U"
t "-o: the header too" "  $H s7 Page 1" 0 sh -c '"$1" -o 2 s7 | sed -n 3p' sh "$U"
t "-e4: input tabs to every 4 columns" "a   b" 0 sh -c 'printf "a\tb\n" | "$1" -t -e4' sh "$U"
t "-ex4: x is the input tab" "a   b" 0 sh -c 'printf "axb\n" | "$1" -t -ex4' sh "$U"
t "-e: every 8" "a       b" 0 sh -c 'printf "a\tb\n" | "$1" -t -e' sh "$U"
t "-i4: spaces to tabs at every 4 columns" "a		b" 0 sh -c 'printf "a       b\n" | "$1" -t -i4' sh "$U"
t "-i,4: , is the output tab" "a,,b" 0 sh -c 'printf "a       b\n" | "$1" -t -i,4' sh "$U"
t "-i: one space stays a space" "abc d" 0 sh -c 'printf "abc d\n" | "$1" -t -i4' sh "$U"
t "one column: long lines are not cut" "200" 0 sh -c 'awk "BEGIN { while (i++ < 200) printf \"x\"; print \"\" }" | "$1" -t | awk "{ print length(\$0) }"' sh "$U"
# columns
t "-2: down the columns, cut to the width" "abcdefghi e${nl}b	  f${nl}c	  g${nl}d" 0 "$U" -t -2 -w 20 s7
t "-3 -a: across" "abcdef b      c${nl}d      e      f${nl}g" 0 "$U" -t -3 -a -w 21 s7
t "-s: a tab between columns" "abcdefghi	e${nl}b	f${nl}c	g${nl}d" 0 "$U" -t -2 -s -w 20 s7
t "-s:" "abcdefghi:e${nl}b:f${nl}c:g${nl}d" 0 "$U" -t -2 -s: -w 20 s7
t "-s: the width 512 by default" "abcdefghijklmnopqrstuvwxyz:e" 0 sh -c '"$1" -t -2 -s: s7 | sed 1q' sh "$U"
t "columns: the width 72 by default" "35" 0 sh -c 'awk "BEGIN { while (i++ < 100) printf \"x\"; print \"\"; print \"y\" }" | "$1" -t -2 | sed 1q | tr -d -c x | wc -c | tr -d " "' sh "$U"
t "columns: a full page fills each column" "line 1 line 11${nl}line 10 line 20${nl}line 41 line 51" 0 sh -c '"$1" -2 -l 20 l60 > o; sed -n 6p o | expand | tr -s " "; sed -n 15p o | expand | tr -s " "; sed -n 46p o | expand | tr -s " "' sh "$U"
t "columns: the last page shares the lines out" "4 3 3" 0 sh -c 'awk "BEGIN { for (i = 1; i <= 10; i++) print i }" | "$1" -t -3 -w 30 | awk "{ n1 += (\$1 != \"\"); n2 += (\$2 != \"\"); n3 += (\$3 != \"\") } END { print n1, n2, n3 }"' sh "$U"
t "columns with -n: the number tab counted in the column" "    1   abcdefghijk     5   e" 0 sh -c '"$1" -t -2 -n -w 40 s7 | sed 1q | expand' sh "$U"
t "columns: the input tabs expanded" "a       b c" 0 sh -c 'printf "a\tb\nc\n" | "$1" -t -2 -w 22 -s" " | sed 1q' sh "$U"
# -m
t "-m: side by side" "abcdefghij abcdefghij${nl}b	   b" 0 sh -c '"$1" -t -m -w 21 s7 s7 | sed 2q' sh "$U"
t "-m: the shorter file ends, the other goes on" "x	   abcdefghij${nl}	   b" 0 sh -c 'echo x > one; "$1" -t -m -w 21 one s7 | sed 2q' sh "$U"
t "-m: no name in the header" "yes" 0 sh -c '"$1" -m s7 s7 | sed -n 3p | grep -E " [0-9]{4}  Page 1\$" >/dev/null && echo yes' sh "$U"
t "-m -n: one number a line" "    1   abcdefghijklmnopqrst abcdefghijklmnopqrst abcdefghijklmnopqrst" 0 sh -c '"$1" -t -m -n s7 s7 s7 | sed 1q | expand | sed "s/ *\$//"' sh "$U"
t "-m: all empty: nothing" "" 0 "$U" -m empty empty
# errors
t "a missing file: a diagnostic, the others written" "abcdefghijklmnopqrstuvwxyz" 1 sh -c '"$1" -t no_such_file s7 2>err | sed 1q; r=$?; grep -q no_such_file err || exit 9; exit 1' sh "$U"
t "-r: no diagnostic" "" 0 sh -c '"$1" -r -t no_such_file 2>&1 >/dev/null | grep .; [ $? = 1 ]' sh "$U"
t "-r: still status 1" "" 1 "$U" -r -t no_such_file
t "-column and -m" "" 2 "$U" -2 -m s7 s7
t "an unknown option" "" 2 "$U" -x s7
t "-l 0" "" 2 "$U" -l 0 s7
t "-w x" "" 2 "$U" -w x s7
t "+0" "" 2 "$U" +0 s7
t "-- then a file" "b" 0 sh -c '"$1" -t -- s7 | sed -n 2p' sh "$U"
t "a file named -: the standard input" "b" 0 sh -c '"$1" -t - < s7 | sed -n 2p' sh "$U"
# options after operands are files
t "options after operands: pr s7 -t: -t is a file" "" 1 sh -c '"$1" s7 -t > /dev/null' sh "$U"
[ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" s7 > /dev/full' sh "$U"

done_tests pr
