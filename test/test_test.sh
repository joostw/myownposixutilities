#!/bin/sh
# test_test.sh - tests for test and [ (POSIX.1-2024).  Run: sh test_test.sh
cd "$(dirname "$0")" || exit 2
pass=0 fail=0
# t NAME EXPECTED-STDOUT EXPECTED-STATUS COMMAND...  (stdout compared without trailing newlines)
t() {
    _n=$1 _e=$2 _s=$3; shift 3
    _o=$("$@" 2>/dev/null); _r=$?
    if [ "$_o" = "$_e" ] && [ "$_r" = "$_s" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s] status %s\n  want: [%s] status %s\n' "$_n" "$_o" "$_r" "$_e" "$_s"; fi
}
done_tests() { echo "$1: $pass passed, $fail failed"; [ "$fail" = 0 ]; }
U=${U:-$PWD/test}
B=${B:-$PWD/[}
# s NAME EXPECTED-STATUS ARGUMENT...: test and [ ... ] both
s() {
    _n=$1 _s=$2; shift 2
    t "$_n" "" "$_s" "$U" "$@"
    t "[ $_n ]" "" "$_s" "$B" "$@" ]
}
T=${TMPDIR:-/tmp}/test_test.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 2
root=false; [ "$(id -u)" = 0 ] && root=true

# by the number of arguments
s "no arguments" 1
s "one: a string" 0 x
s "one: empty" 1 ""
s "one: an operator as a string" 0 -n
s "one: !" 0 "!"
s "one: -a" 0 -a
s "two: ! string" 1 "!" x
s "two: ! empty" 0 "!" ""
s "two: -n" 0 -n x
s "two: -n empty" 1 -n ""
s "two: -z" 1 -z x
s "two: -z empty" 0 -z ""
s "three: =" 0 a = a
s "three: = differs" 1 a = b
s "three: !=" 0 a != b
s "three: binary before !" 1 "!" = a
s "three: ! with a unary" 1 "!" -n x
s "three: ! ! x" 0 "!" "!" x
s "three: ( x )" 0 "(" x ")"
s "three: ( empty )" 1 "(" "" ")"
s "three: -a as a binary? no: = first" 0 -a = -a
s "four: ! with a binary" 1 "!" a = a
s "four: ! ( x )" 1 "!" "(" x ")"
s "four: ( ! x )" 1 "(" "!" x ")"
s "four: ( -n x )" 0 "(" -n x ")"

# strings
s "= of the empty string" 0 "" = ""
s "!= ' a' a" 0 " a" != a
s "<" 0 a "<" b
s "< no" 1 b "<" a
s ">" 0 b ">" a
s "> equal" 1 a ">" a
s "= is bytes" 1 abc = ABC

# integers
s "-eq" 0 5 -eq 5
s "-eq signs" 0 -0 -eq +0
s "-ne" 0 5 -ne 6
s "-gt" 0 6 -gt 5
s "-ge" 0 5 -ge 5
s "-lt" 0 -6 -lt 5
s "-le" 1 6 -le 5
s "-eq with blanks" 0 " 5 " -eq 5
s "-eq large" 0 9223372036854775807 -gt 9223372036854775806
s "not an integer" 2 a -eq 1
s "not an integer: 1x" 2 1x -eq 1
s "not an integer: empty" 2 "" -eq 0
s "out of range" 2 99999999999999999999 -eq 1
s "a decimal point" 2 1.0 -eq 1

# files
: > empty; echo x > full; mkdir dir; ln -s full lnk; ln -s nowhere dangling; mkfifo fifo
chmod 644 full; : > exe; chmod 755 exe; : > suid; chmod 4755 suid; : > sgid; chmod 2755 sgid
s "-e" 0 -e full
s "-e missing" 1 -e nofile
s "-e a link: its target" 0 -e lnk
s "-e a dangling link" 1 -e dangling
s "-f" 0 -f full
s "-f a directory" 1 -f dir
s "-f through a link" 0 -f lnk
s "-d" 0 -d dir
s "-d a file" 1 -d full
s "-h" 0 -h lnk
s "-L" 0 -L dangling
s "-h a file" 1 -h full
s "-h missing" 1 -h nofile
s "-p" 0 -p fifo
s "-p a file" 1 -p full
s "-c" 0 -c /dev/null
s "-c a file" 1 -c full
s "-b /dev/null" 1 -b /dev/null
s "-S a file" 1 -S full
s "-s" 0 -s full
s "-s empty" 1 -s empty
s "-s missing" 1 -s nofile
s "-r" 0 -r full
s "-w" 0 -w full
s "-x" 0 -x exe
s "-x a directory" 0 -x dir
s "-x not executable" 1 -x full
s "-x missing" 1 -x nofile
s "-u" 0 -u suid
s "-u no" 1 -u exe
s "-g" 0 -g sgid
s "-g no" 1 -g suid
if ! $root; then
    : > nr; chmod 0 nr
    s "-r no permission" 1 -r nr
    s "-w no permission" 1 -w nr
fi
s "-t on a pipe" 1 -t 0
t "-t not a number" "" 2 "$U" -t x
s "-t a closed descriptor" 1 -t 9
s "-t a large number" 1 -t 99999
touch -t 200001010000 old; touch -t 201001010000 new
s "-nt" 0 new -nt old
s "-nt no" 1 old -nt new
s "-nt the other missing" 0 new -nt nofile
s "-nt this one missing" 1 nofile -nt new
s "-ot" 0 old -ot new
s "-ot the first missing" 0 nofile -ot old
s "-ot both missing" 1 nofile -ot nofile2
s "-nt equal times" 1 new -nt new
s "-ef" 0 full -ef lnk
ln full hard 2>/dev/null
s "-ef a hard link" 0 full -ef hard
s "-ef different" 1 full -ef empty
s "-ef missing" 1 full -ef nofile

# more than four arguments: -a, -o, ( ), !
s "-a" 0 x -a y
s "-a false" 1 x -a ""
s "-o" 0 "" -o y
s "-a binds tighter than -o" 0 x -o "" -a ""
s "( ) group" 1 "(" x -o "" ")" -a ""
s "! in the expression" 0 "!" "" -a x
s "five arguments" 0 a = a -a -n b
s "six arguments" 0 "(" a = b ")" -o -n x
s "a missing )" 2 "(" a = a -a b
s "too many arguments" 2 a b c d e
s "an operand missing" 2 x -a
s "nested" 0 "(" "(" a ")" ")" -a "(" b ")"

# [
t "[ without ]" "" 2 "$B" x
t "[ ] alone" "" 1 "$B" ]
t "[ ] ]" "" 0 "$B" ] ]
t "test with ]" "" 0 "$U" ]
t "--: a string" "" 0 "$U" --
t "-- = --" "" 0 "$U" -- = --

done_tests test
