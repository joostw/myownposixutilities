#!/bin/sh
# test_head.sh - tests for head (POSIX.1-2024).  Run: sh test_head.sh
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
U=${U:-$PWD/head}
T=${TMPDIR:-/tmp}/test_head.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
nums() { awk -v a="$1" -v b="$2" 'BEGIN { if (a <= b) for (i = a; i <= b; i++) print i; else for (i = a; i >= b; i--) print i }'; }  # like seq
nums 1 20 > $T/s; nums 1 100000 > $T/big; printf 'a\nb\nc' > $T/nonl
t 'default 10 lines' "$(nums 1 10)" 0 $U $T/s
t '-n 3' "$(nums 1 3)" 0 $U -n 3 $T/s
t '-n3' "$(nums 1 3)" 0 $U -n3 $T/s
t '-c 5' "1
2
3" 0 $U -c 5 $T/s
t 'more than the file' "$(nums 1 20)" 0 $U -n 100 $T/s
t 'no final newline' 'a
b
c' 0 $U -n 5 $T/nonl
t 'standard input' "$(nums 1 2)" 0 sh -c "cat $T/s | $U -n 2"
t 'headers for several files' "==> $T/s <==
1

==> $T/nonl <==
a" 0 $U -n 1 $T/s $T/nonl
t '- in a header' "==> - <==
1

==> $T/nonl <==
a" 0 sh -c "printf '1\\n2\\n3\\n' | $U -n 1 - $T/nonl"
t 'missing file' '' 1 $U $T/none
t '-n 0 is not positive' '' 1 $U -n 0 $T/s
t '-n with a sign' '' 1 $U -n +3 $T/s
t '-n not a number' '' 1 $U -n x $T/s
[ -c /dev/full ] && t 'write error' '' 1 sh -c "$U $T/s >/dev/full"
t 'binary with -c' '3' 0 sh -c "printf 'a\0b\377' | $U -c 3 | wc -c | tr -d ' '"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: head f -n: -n is a file" "==> f <==
x" 1 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" f -n; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests head
