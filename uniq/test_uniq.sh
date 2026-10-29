#!/bin/sh
# test_uniq.sh - tests for uniq (POSIX.1-2024).  Run: sh test_uniq.sh
cd "$(dirname "$0")" || exit 2
pass=0 fail=0
# t NAME EXPECTED-STDOUT EXPECTED-STATUS COMMAND...  (stdout compared without trailing newlines)
t() {
    _n=$1 _e=$2 _s=$3; shift 3
    _o=$("$@" 2>/dev/null); _r=$?
    if [ "$_o" = "$_e" ] && [ "$_r" = "$_s" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s] status %s\n  want: [%s] status %s\n' "$_n" "$_o" "$_r" "$_e" "$_s"; fi
}
# tb NAME EXPECTED-OD-C COMMAND...  (exact bytes of stdout, as od -c)
tb() {
    _n=$1 _e=$2; shift 2
    _o=$("$@" 2>/dev/null | od -An -c | tr -s ' \n' ' ' | sed 's/^ //;s/ $//')
    if [ "$_o" = "$_e" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  %s\n  want: %s\n' "$_n" "$_o" "$_e"; fi
}
# ok NAME CONDITION-COMMAND...
ok() {
    _n=$1; shift
    if "$@"; then pass=$((pass+1)); else fail=$((fail+1)); printf 'FAIL %s\n' "$_n"; fi
}
done_tests() { echo "$1: $pass passed, $fail failed"; [ "$fail" = 0 ]; }
U=${U:-$PWD/uniq}
# a UTF-8 locale for the multibyte tests, which are skipped without one (POSIX
# names none): $UTF8_LOCALE, or C.UTF-8, or any UTF-8 locale that locale -a lists
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
export U8
u8() { [ -n "$U8" ]; }
T=${TMPDIR:-/tmp}/test_uniq.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
printf 'a\na\nb\nc\nc\nc\na\n' > $T/in
t 'default' 'a
b
c
a' 0 $U $T/in
t '-c' '2 a
1 b
3 c
1 a' 0 $U -c $T/in
t '-d' 'a
c' 0 $U -d $T/in
t '-u' 'b
a' 0 $U -u $T/in
t '-d -u: nothing' '' 0 $U -d -u $T/in
t '-c -d' '2 a
3 c' 0 $U -c -d $T/in
t '-f 1' 'x a
y b' 0 sh -c "printf 'x a\ny a\ny b\n' | $U -f 1"
t '-f more fields than the line' 'x' 0 sh -c "printf 'x\ny\n' | $U -f 3"
t '-s 1' 'xa
xb' 0 sh -c "printf 'xa\nya\nxb\n' | $U -s 1"
t '-f then -s' 'p xa
p yb' 0 sh -c "printf 'p xa\nq ya\np yb\n' | $U -f 1 -s 2"
u8 && t '-s counts characters' "xéa
xéb" 0 sh -c "printf 'x\303\251a\ny\303\251a\nx\303\251b\n' | LC_ALL=$U8 $U -s 2"
t 'trailing newline ignored' 'a' 0 sh -c "printf 'a\na' | $U"
t 'output file' 'a
b
c
a' 0 sh -c "$U $T/in $T/out && cat $T/out"
t '- as input and output' 'a
b
c
a' 0 sh -c "$U - - < $T/in"
t 'too many operands' '' 1 $U $T/in $T/o2 extra
t 'missing input' '' 1 $U $T/none
t 'unknown option' '' 1 $U -z $T/in
[ -c /dev/full ] && t 'write error' '' 1 sh -c "$U $T/in >/dev/full"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: uniq f -c: -c is the output file" "x" 0 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" f -c && cat ./-c; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests uniq
