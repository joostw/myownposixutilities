#!/bin/sh
# test_logname.sh - tests for logname (POSIX.1-2024).  Run: sh test_logname.sh
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
U=${U:-$PWD/logname}
T=${TMPDIR:-/tmp}/test_logname.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# the reference: the system's logname (getlogin() too); the login name may
# be unknown here (no utmp entry for the terminal): then both fail
want=$(logname 2>/dev/null); st=$?
[ "$st" = 0 ] || st=1
t "the login name, as the system's logname" "$want" $st "$U"
if [ "$st" = 0 ]; then
    t "not from LOGNAME" "$want" 0 env LOGNAME=nobody_x9 "$U"
else
    t "no login name: a diagnostic" "" 0 sh -c '"$1" 2>&1 >/dev/null | grep -q .' sh "$U"
    t "no login name: not from LOGNAME" "" 1 env LOGNAME=nobody_x9 "$U"
fi
t "--" "$want" $st "$U" --
t "an operand" "" 2 "$U" x
t "an option" "" 2 "$U" -x
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: logname x -x: x is an operand" "" 2 "$U" x -x
[ "$st" = 0 ] && [ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" > /dev/full' sh "$U"

done_tests logname
