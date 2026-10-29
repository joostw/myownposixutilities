#!/bin/sh
# test_sort.sh - tests for sort (POSIX.1-2024).  Run: sh test_sort.sh
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
U=${U:-$PWD/sort}
# a UTF-8 locale for the cases with characters outside ASCII (POSIX
# names none): $UTF8_LOCALE, or C.UTF-8, or any one that locale -a lists
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
u8() { [ -n "$U8" ]; }
T=${TMPDIR:-/tmp}/test_sort.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
nl='
'
# s NAME INPUT EXPECTED OPTION...: sort INPUT (lines joined by |) with the options
s() {
    _n=$1 _in=$2 _ex=$3; shift 3
    printf '%s\n' "$_in" | tr '|' '\n' > in
    t "$_n" "$(printf '%s' "$_ex" | tr '|' '\n')" 0 "$U" "$@" in
}

# whole lines
s "lines" "c|a|b" "a|b|c"
s "bytes in the C locale" "b|B|a|A|_|1" "1|A|B|_|a|b"
s "-r" "a|c|b" "c|b|a" -r
s "empty lines first" "b||a" "|a|b"
s "a prefix sorts first" "ab|a|abc" "a|ab|abc"
s "duplicates kept" "b|a|b" "a|b|b"
s "-u" "b|a|b|a" "a|b" -u
s "-f" "b|B|a|A" "A|a|B|b" -f
s "-f -u" "b|B|a" "a|b" -fu
s "-d" "a-c|ab|a b" "a b|ab|a-c" -d
s "-i" "$(printf 'b\001|a\002c|ab')" "$(printf 'ab|a\002c|b\001')" -i
s "-b" "  b| a|c" " a|  b|c" -b
s "no -b: blanks count" "  b| a|c" "  b| a|c"

# -n
s "-n" "10|9|-1|0|1.5|-2.5" "-2.5|-1|0|1.5|9|10" -n
s "-n leading zeros; equal: the line decides" "010|9|0010.0" "9|0010.0|010" -n
s "-n -0 and 0" "0|-0|00" "-0|0|00" -n
s "-n empty is zero" "1||-1|x" "-1||x|1" -n
s "-n leading blanks" "  10| 9|8" "8| 9|  10" -n
s "-n stops at the first non-digit" "1x9|1a|2" "1a|1x9|2" -n
s "-n decimals" "1.10|1.09|1.1|1." "1.|1.09|1.1|1.10" -n
s "-n -r" "1|3|2" "3|2|1" -nr
s "-n large numbers" "123456789012345678901234567890|99|-123456789012345678901234567891" "-123456789012345678901234567891|99|123456789012345678901234567890" -n
s "-n no + sign" "+5|3" "+5|3" -n

# fields and keys
s "-k 2" "a 3|b 1|c 2" "b 1|c 2|a 3" -k 2
s "-k 2n" "a 10|b 9|c 100" "b 9|a 10|c 100" -k 2n
s "-k 2,2 then the line" "x 1 b|y 1 a|w 0 z" "w 0 z|x 1 b|y 1 a" -k 2,2
s "-k 2,2 -k 1r" "x 1|y 1|w 0" "w 0|y 1|x 1" -k 2,2 -k 1r
s "-t" "b:2|a:3|c:1" "c:1|b:2|a:3" -t : -k 2
s "-t empty fields" "a::2|b:1:|c:3:1" "b:1:|c:3:1|a::2" -t : -k 3
s "-t the separator is not in the field" "a!c|a:b" "a:b|a!c" -t : -k 1,1
s "-k 1.2" "xb|ya|zc" "ya|xb|zc" -k 1.2
s "-k 1.2,1.2" "xba|yab|zaa" "yab|zaa|xba" -k 1.2,1.2
s "-k 2.2: blanks count without b" "a  ab|b xb" "a  ab|b xb" -k 2.2
s "-k 2.2b" "a  bc|b xa" "b xa|a  bc" -k 2.2b
s "-b before -k applies to it" "a  bc|b xa" "b xa|a  bc" -b -k 2.2
s "a key past the end of the line: empty" "b|a x|c" "b|c|a x" -k 2
s "a key ending before it starts: empty" "b a|a b" "a b|b a" -k 2,1
s "-k 1,1 vs whole line: equal keys" "a 2|a 1" "a 1|a 2" -k 1,1
s "-k 1,1 -u keeps the first" "a 2|a 1|b 0" "a 2|b 0" -u -k 1,1
s "-k with modifiers ignores the global ones" "B|a|C" "a|C|B" -f -k 1r,1r -k 1
s "-k 3,3n -k 1,1" "b x 2|a y 2|c z 1" "c z 1|a y 2|b x 2" -k 3,3n -k 1,1
s "-r reverses the last comparison too" "a 1|b 1" "b 1|a 1" -r -k 2,2
s "-k 2,2.0: the whole field" "a xyz|b xya" "b xya|a xyz" -k 2,2.0
s "fields: leading blanks belong to field 1" " b a| a b" " b a| a b" -k 2
s "a tab is a blank, part of the field" "$(printf 'b y|a\tz')" "$(printf 'a\tz|b y')" -k 2
s "-b: the blanks are skipped" "$(printf 'a\tz|b y')" "$(printf 'b y|a\tz')" -k 2b

# input and output
printf 'c\nb\n' > f1; printf 'a\nd\n' > f2
t "several files" "a${nl}b${nl}c${nl}d" 0 "$U" f1 f2
t "- is standard input" "a${nl}b${nl}c${nl}d" 0 sh -c '"$1" f1 - < f2' sh "$U"
t "no newline at the end" "a${nl}b" 0 sh -c 'printf "b\na" | "$1"' sh "$U"
t "-o" "a${nl}b${nl}c${nl}d" 0 sh -c '"$1" -o out f1 f2 && cat out' sh "$U"
t "-o an input file" "b${nl}c" 0 sh -c 'cp f1 g && "$1" -o g g && cat g' sh "$U"
t "options after the operands" "d${nl}c${nl}b${nl}a" 0 "$U" f1 f2 -r
t "-- ends the options" "x" 0 sh -c 'echo x > ./-r && "$1" -- -r' sh "$U"
t "-m" "a${nl}b${nl}c${nl}d${nl}e" 0 sh -c 'printf "a\nc\ne\n" > m1; printf "b\nd\n" > m2; "$1" -m m1 m2' sh "$U"
t "-m -u" "a${nl}b${nl}c" 0 sh -c 'printf "a\nb\n" > m1; printf "a\nb\nc\n" > m2; "$1" -mu m1 m2' sh "$U"
t "-m -o an input" "a${nl}b${nl}c" 0 sh -c 'printf "a\nc\n" > m3; printf "b\n" > m4; "$1" -m -o m3 m3 m4 && cat m3' sh "$U"
t "-m -n" "1${nl}2${nl}10" 0 sh -c 'printf "2\n10\n" > m5; printf "1\n" > m6; "$1" -mn m5 m6' sh "$U"
awk 'BEGIN { srand(7); for (i = 0; i < 20000; i++) print int(rand() * 1000) }' > many
t "many lines" "" 0 sh -c '"$1" -n many > sorted && "$1" -cn sorted' sh "$U"

# -c and -C
printf 'a\nb\nb\nc\n' > ok; printf 'a\nc\nb\n' > notok
t "-c sorted" "" 0 "$U" -c ok
t "-c not sorted" "" 1 "$U" -c notok
te "-c the message" "sort: notok:3: disorder: b" "$U" -c notok
t "-C not sorted" "" 1 "$U" -C notok
te "-C no message" "" "$U" -C notok
t "-c -u duplicates" "" 1 "$U" -cu ok
te "-c -u the message" "sort: ok:3: duplicate key: b" "$U" -cu ok
t "-c with keys" "" 0 sh -c 'printf "b 1\na 2\n" | "$1" -c -k 2n' sh "$U"
t "-c standard input" "" 1 sh -c 'printf "b\na\n" | "$1" -c' sh "$U"
t "-c two files" "" 2 "$U" -c ok ok

# errors
t "a missing file" "" 2 "$U" nofile
t "a bad option" "" 2 "$U" -x f1
t "a bad key" "" 2 "$U" -k 0 f1
t "a bad key: a letter" "" 2 "$U" -k 1x f1
t "-t of two characters" "" 2 "$U" -t ab f1
t "-k without an argument" "" 2 "$U" -k
[ -c /dev/full ] && t "a write error" "" 2 sh -c '"$1" f1 >/dev/full' sh "$U"

# characters of more than one byte
if u8; then
    printf 'éb\nea\nza\n' > mb
    t "UTF-8: -k 1.2 counts characters" "ea${nl}za${nl}éb" 0 env LC_ALL=$U8 "$U" -k 1.2 mb
    printf 'é\nÉ\nf\n' > mb2
    t "UTF-8: -f folds é" "f${nl}É${nl}é" 0 env LC_ALL=$U8 "$U" -f -k 1,1 mb2
    t "UTF-8: -f -u folds é, keeps the first" "f${nl}é" 0 env LC_ALL=$U8 "$U" -fu mb2
    printf 'x€2\ny€1\n' > mb3
    t "UTF-8: -t of a multibyte character" "y€1${nl}x€2" 0 env LC_ALL=$U8 "$U" -t € -k 2 mb3
    printf 'aé\nab\n' > mb4
    t "UTF-8: -d keeps letters outside ASCII" "ab${nl}aé" 0 env LC_ALL=$U8 "$U" -d mb4
fi

done_tests sort
