#!/bin/sh
# test_uname.sh - tests for uname (POSIX.1-2024).  Run: sh test_uname.sh
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
U=${U:-$PWD/uname}
T=${TMPDIR:-/tmp}/test_uname.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# the reference: the system's uname, with the options POSIX gives it
for o in s n r v m; do
    eval "v_$o=\$(uname -$o)"
done
t "no options: -s" "$v_s" 0 "$U"
t "-s" "$v_s" 0 "$U" -s
t "-n" "$v_n" 0 "$U" -n
t "-r" "$v_r" 0 "$U" -r
t "-v" "$v_v" 0 "$U" -v
t "-m" "$v_m" 0 "$U" -m
t "-a: all five" "$v_s $v_n $v_r $v_v $v_m" 0 "$U" -a
t "the order of the fields, not of the options" "$v_s $v_r $v_m" 0 "$U" -m -r -s
t "-msr" "$v_s $v_r $v_m" 0 "$U" -msr
t "-a with others" "$v_s $v_n $v_r $v_v $v_m" 0 "$U" -n -a -m
t "-sn" "$v_s $v_n" 0 "$U" -sn
t "--" "$v_s" 0 "$U" --
t "an operand" "" 2 "$U" x
t "an unknown option" "" 2 "$U" -p
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: uname x -a: -a is an operand" "" 2 "$U" x -a
[ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" -a > /dev/full' sh "$U"

done_tests uname
