#!/bin/sh
# test_cal.sh - tests for cal (POSIX.1-2024).  Run: sh test_cal.sh
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
U=${U:-$PWD/cal}
T=${TMPDIR:-/tmp}/test_cal.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
t "September 1752: the eleven days left out" "   September 1752
Su Mo Tu We Th Fr Sa
       1  2 14 15 16
17 18 19 20 21 22 23
24 25 26 27 28 29 30" 0 "$U" 9 1752
t "February 2024: a leap year" "   February 2024
Su Mo Tu We Th Fr Sa
             1  2  3
 4  5  6  7  8  9 10
11 12 13 14 15 16 17
18 19 20 21 22 23 24
25 26 27 28 29" 0 "$U" 2 2024
t "February 1900: not a leap year (Gregorian)" "25 26 27 28" 0 sh -c '"$1" 2 1900 | sed -n "\$p"' sh "$U"
t "February 1700: a leap year (Julian before 1752)" "29" 0 sh -c '"$1" 2 1700 | sed -n "\$p" | awk "{ print \$NF }"' sh "$U"
t "February 2000: a leap year" "29" 0 sh -c '"$1" 2 2000 | sed -n "\$p" | awk "{ print \$NF }"' sh "$U"
t "February 1800: not a leap year" "28" 0 sh -c '"$1" 2 1800 | sed -n "\$p" | awk "{ print \$NF }"' sh "$U"
t "January 1, 2000 was a Saturday" "                   1" 0 sh -c '"$1" 1 2000 | sed -n 3p' sh "$U"
t "January 1 of the year 1: Julian" "     January 1
Su Mo Tu We Th Fr Sa
                   1
 2  3  4  5  6  7  8
 9 10 11 12 13 14 15
16 17 18 19 20 21 22
23 24 25 26 27 28 29
30 31" 0 "$U" 1 1
t "December 9999" "   December 9999
Su Mo Tu We Th Fr Sa
          1  2  3  4
 5  6  7  8  9 10 11
12 13 14 15 16 17 18
19 20 21 22 23 24 25
26 27 28 29 30 31" 0 "$U" 12 9999
t "October 1582 is Julian here" "    October 1582
Su Mo Tu We Th Fr Sa
    1  2  3  4  5  6" 0 sh -c '"$1" 10 1582 | sed 3q' sh "$U"
t "83 is the year 83" "     January 83" 0 sh -c '"$1" 1 83 | sed 1q' sh "$U"
t "a month with a leading zero" "$("$U" 9 2024)" 0 "$U" 09 2024
# a year
t "a year: the title" "                              2024" 0 sh -c '"$1" 2024 | sed 1q' sh "$U"
t "a year: the first row of months" "
      January               February               March
Su Mo Tu We Th Fr Sa  Su Mo Tu We Th Fr Sa  Su Mo Tu We Th Fr Sa
    1  2  3  4  5  6               1  2  3                  1  2
 7  8  9 10 11 12 13   4  5  6  7  8  9 10   3  4  5  6  7  8  9
14 15 16 17 18 19 20  11 12 13 14 15 16 17  10 11 12 13 14 15 16
21 22 23 24 25 26 27  18 19 20 21 22 23 24  17 18 19 20 21 22 23
28 29 30 31           25 26 27 28 29        24 25 26 27 28 29 30
                                            31" 0 sh -c '"$1" 2024 | sed -n 2,10p' sh "$U"
t "a year: four rows of three" "4" 0 sh -c '"$1" 2024 | grep -c "Su Mo Tu We Th Fr Sa  Su Mo Tu We Th Fr Sa  Su Mo Tu We Th Fr Sa"' sh "$U"
t "a year: as its months" "same" 0 sh -c '
for m in 1 2 3 4 5 6 7 8 9 10 11 12; do "$1" $m 1752 | sed 1d; done | grep -v "^Su" | tr -s " " "\n" | grep . > a
"$1" 1752 | sed 1d | grep -v "^ *[A-Z]" | grep -v "^Su" | awk "{ for (i = 1; i <= NF; i++) print \$i }" | sort -n > b
sort -n a | cmp -s - b && echo same' sh "$U"
t "a year: no trailing blanks" "" 1 sh -c '"$1" 2024 | grep " \$"' sh "$U"
t "a month: no trailing blanks" "" 1 sh -c '"$1" 6 2024 | grep " \$"' sh "$U"
t "the year 1752: 355 days" "355" 0 sh -c '"$1" 1752 | sed 1d | grep -v "^ *[A-Z]" | grep -v "^Su" | awk "{ n += NF } END { print n }"' sh "$U"
# no operands: this month
t "no operands: the current month" "same" 0 sh -c 'a=$("$1"); b=$("$1" $(date +%m) $(date +%Y)); [ "$a" = "$b" ] && echo same' sh "$U"
t "no operands: the month as date names it" "same" 0 sh -c 'a=$("$1" | sed 1q | awk "{ print \$1, \$2 }"); [ "$a" = "$(date "+%B %Y")" ] && echo same' sh "$U"
# errors
t "month 0" "" 1 "$U" 0 2024
t "month 13" "" 1 "$U" 13 2024
t "year 0" "" 1 "$U" 0
t "year 10000" "" 1 "$U" 10000
t "a month that is no number" "" 1 "$U" x 2024
t "a year that is no number" "" 1 "$U" 2024x
t "a negative year" "" 1 "$U" 1 -5
t "three operands" "" 2 "$U" 1 2 2024
t "an option" "" 2 "$U" -y
t "--" "$("$U" 2024)" 0 "$U" -- 2024
# there are no options: an option after an operand is an operand
t "options after operands: cal 2024 -y: -y is an operand" "" 1 "$U" 2024 -y
[ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" 2024 > /dev/full' sh "$U"

done_tests cal
