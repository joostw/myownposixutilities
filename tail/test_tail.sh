#!/bin/sh
# test_tail.sh - tests for tail (POSIX.1-2024).  Run: sh test_tail.sh
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
U=${U:-$PWD/tail}
T=${TMPDIR:-/tmp}/test_tail.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
nums() { awk -v a="$1" -v b="$2" 'BEGIN { if (a <= b) for (i = a; i <= b; i++) print i; else for (i = a; i >= b; i--) print i }'; }  # like seq
nums 1 20 > $T/s; nums 1 100000 > $T/big; printf 'a\nb\nc' > $T/nonl
t 'default 10 lines' "$(nums 11 20)" 0 $U $T/s
t '-n 3' "$(nums 18 20)" 0 $U -n 3 $T/s
t '-n -3' "$(nums 18 20)" 0 $U -n -3 $T/s
t '-n +18' "$(nums 18 20)" 0 $U -n +18 $T/s
t '-n +1 whole file' "$(nums 1 20)" 0 $U -n +1 $T/s
t '-n 0 copies nothing' '' 0 $U -n 0 $T/s
t '-n -0 copies nothing' '' 0 $U -n -0 $T/s
t '-n +0 is an error' '' 1 $U -n +0 $T/s
t '-c 5' '9
20' 0 $U -c 5 $T/s
t '-c +49' '20' 0 $U -c +49 $T/s
t '-c 0' '' 0 $U -c 0 $T/s
t 'no final newline' 'b
c' 0 $U -n 2 $T/nonl
t 'from a pipe' "$(nums 99998 100000)" 0 sh -c "cat $T/big | $U -n 3"
t 'from a pipe, from the start' "$(nums 99999 100000)" 0 sh -c "cat $T/big | $U -n +99999"
t 'bytes from a pipe' '00' 0 sh -c "cat $T/big | $U -c 3"
t 'a huge count' '100000' 0 sh -c "cat $T/big | $U -n 999999999999 | wc -l | tr -d ' '"
t 'a huge byte count' '588895' 0 sh -c "cat $T/big | $U -c 99999999999 | wc -c | tr -d ' '"
t '+N from the current offset' '3' 0 sh -c "{ read x; $U -n +2; } < $T/s | head -1"
t '-r' "$(nums 20 1)" 0 $U -r $T/s
t '-r -n 3' '20
19
18' 0 $U -r -n 3 $T/s
t '-r -n with a sign' '' 1 $U -r -n +3 $T/s
t '-r with -c' '' 1 $U -r -c 3 $T/s
t '-f ignored for a pipe' '20' 0 sh -c "cat $T/s | timeout 3 $U -f -n 1"
t '-f follows a file' '1
2
more' 0 sh -c "printf '1\\n2\\n' > $T/g; ($U -f $T/g & p=\$!; sleep 2; echo more >> $T/g; sleep 2; kill \$p) 2>/dev/null"
t 'two operands' '' 1 $U $T/s $T/s
t 'missing file' '' 1 $U $T/none
[ -c /dev/full ] && t 'write error' '' 1 sh -c "$U $T/s >/dev/full"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: tail f -r: two operands" "" 1 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" f -r; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests tail
