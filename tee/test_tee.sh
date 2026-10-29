#!/bin/sh
# test_tee.sh - tests for tee (POSIX.1-2024).  Run: sh test_tee.sh
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
U=${U:-$PWD/tee}
T=${TMPDIR:-/tmp}/test_tee.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
t 'copies to stdout' 'hi' 0 sh -c "echo hi | $U"
t 'and to files' 'hi
hi
hi' 0 sh -c "echo hi | $U $T/f1 $T/f2 && cat $T/f1 $T/f2"
t '-a appends' 'old
hi' 0 sh -c "echo old > $T/f3; echo hi | $U -a $T/f3 >/dev/null; cat $T/f3"
t 'truncates without -a' 'hi' 0 sh -c "echo old > $T/f4; echo hi | $U $T/f4 >/dev/null; cat $T/f4"
t '- is a file named -' 'hi
hi' 0 sh -c "cd $T && echo hi | $U - && cat ./-"
t 'unopenable file: the others still written' 'hi
hi' 1 sh -c "echo hi | $U $T/nodir/x $T/f5; s=\$?; cat $T/f5; exit \$s"
t '13 files' '13' 0 sh -c "cd $T && echo z | $U a1 a2 a3 a4 a5 a6 a7 a8 a9 a10 a11 a12 a13 >/dev/null && cat a* | wc -l | tr -d ' '"
[ -c /dev/full ] && t 'stdout write error: files still written' 'hi' 1 sh -c "echo hi | $U $T/f6 >/dev/full; s=\$?; cat $T/f6; exit \$s"
t '-i ignores SIGINT' 'done' 0 sh -c "(sleep 2; echo done) | $U -i & p=\$!; sleep 1; kill -s INT \$p 2>/dev/null; wait \$p"
t 'unknown option' '' 1 sh -c "echo | $U -z"
t 'binary data' '6' 0 sh -c "printf 'a\0b\377\n\0' | $U | wc -c | tr -d ' '"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: tee g -a: -a is a file" "y
y" 0 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { echo y | "$1" g -a && cat ./-a; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests tee
