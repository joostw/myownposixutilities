#!/bin/sh
# test_wc.sh - tests for wc (POSIX.1-2024).  Run: sh test_wc.sh
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
U=${U:-$PWD/wc}
# a UTF-8 locale for the multibyte tests, which are skipped without one (POSIX
# names none): $UTF8_LOCALE, or C.UTF-8, or any UTF-8 locale that locale -a lists
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
export U8
u8() { [ -n "$U8" ]; }
T=${TMPDIR:-/tmp}/test_wc.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
printf 'one two\nthree\n' > $T/a; printf 'x y' > $T/b; printf 'caf\303\251 \303\251t\303\251\n' > $T/u; printf 'caf\351 x\n' > $T/l1
t 'default' "2 3 14 $T/a" 0 $U $T/a
t '-l' "2 $T/a" 0 $U -l $T/a
t '-w' "3 $T/a" 0 $U -w $T/a
t '-c' "14 $T/a" 0 $U -c $T/a
t 'order is l w c' "2 3 14 $T/a" 0 $U -c -w -l $T/a
t 'no final newline' "0 2 3 $T/b" 0 $U $T/b
t 'total' "2 3 14 $T/a
0 2 3 $T/b
2 5 17 total" 0 $U $T/a $T/b
t 'standard input: no name' '2 3 14' 0 sh -c "$U < $T/a"
t '- is named' "2 3 14 -" 0 sh -c "$U - < $T/a"
u8 && t '-m in UTF-8' "9 $T/u" 0 env LC_ALL=$U8 $U -m $T/u
u8 && t '-c in UTF-8' "12 $T/u" 0 env LC_ALL=$U8 $U -c $T/u
t '-m in the C locale' "12 $T/u" 0 env LC_ALL=C $U -m $T/u
u8 && t '-m with an invalid byte goes on' "6 $T/l1" 0 env LC_ALL=$U8 $U -m $T/l1
u8 && t 'words in UTF-8' "2 $T/u" 0 env LC_ALL=$U8 $U -w $T/u
t 'missing file' '' 1 $U $T/none
t 'unknown option' '' 1 $U -z $T/a
[ -c /dev/full ] && t 'write error' '' 1 sh -c "$U $T/a >/dev/full"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: wc f -l: -l is a file" "1 1 2 f
1 1 2 total" 1 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" f -l; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests wc
