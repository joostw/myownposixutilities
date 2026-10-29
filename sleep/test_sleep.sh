#!/bin/sh
# test_sleep.sh - tests for sleep (POSIX.1-2024).  Run: sh test_sleep.sh
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
U=${U:-$PWD/sleep}
t 'zero' '' 0 $U 0
t 'one second' '' 0 $U 1
t '-- discarded' '' 0 $U -- 0
t 'many digits' '' 0 $U 000
t 'negative' '' 1 $U -1
t 'sign' '' 1 $U +1
t 'blank' '' 1 $U ' 1'
t 'fraction' '' 1 $U 0.5
t 'empty' '' 1 $U ''
t 'no operand' '' 1 $U
t 'two operands' '' 1 $U 1 2
t 'huge value still sleeps' '124' 0 sh -c "timeout 1 $U 99999999999999999999999; echo \$?"
t 'SIGALRM ignored' 'ok' 0 sh -c "$U 2 & p=\$!; sleep 1; kill -s ALRM \$p; wait \$p && echo ok"
ok 'sleeps at least the time' sh -c "s=\$(date +%s); $U 2; e=\$(date +%s); [ \$((e - s)) -ge 2 ]"
done_tests sleep
