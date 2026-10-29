#!/bin/sh
# test_cat.sh - tests for cat (POSIX.1-2024).  Run: sh test_cat.sh
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
U=${U:-$PWD/cat}
T=${TMPDIR:-/tmp}/test_cat.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
printf 'one\ntwo\n' > $T/a; printf 'x' > $T/b; printf 'a\0b\377' > $T/bin
t 'one file' 'one
two' 0 $U $T/a
t 'files in order' 'one
two
xone
two' 0 $U $T/a $T/b $T/a
tb 'binary data' 'a \0 b 377' $U $T/bin
t 'standard input' 'in' 0 sh -c "echo in | $U"
t '- in the sequence' 'xin
x' 0 sh -c "echo in | $U $T/b - $T/b"
t '- twice: the second reads after end of file' 'in' 0 sh -c "echo in | $U - -"
t '-u' 'one
two' 0 $U -u $T/a
t 'missing file: others still copied' 'xx' 1 $U $T/b $T/none $T/b
t 'unknown option' '' 1 $U -z $T/a
[ -c /dev/full ] && t 'write error' '' 1 sh -c "$U $T/a >/dev/full"
t '-- ends options' 'x' 0 $U -- $T/b
ok '-u writes without delay' sh -c "(printf a; sleep 2; printf b) | $U -u | { s=\$(date +%s); head -c 1 >/dev/null; e=\$(date +%s); [ \$((e - s)) -lt 2 ]; }"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: cat f -u: -u is a file" "x" 1 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" f -u; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests cat
