#!/bin/sh
# test_tty.sh - tests for tty (POSIX.1-2024).  Run: sh test_tty.sh
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
U=${U:-$PWD/tty}
T=${TMPDIR:-/tmp}/test_tty.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
# ptyrun FDS COMMAND...: COMMAND with the descriptors FDS on a terminal (../testlib/ptyrun.py)
TL=$(cd ../testlib && pwd) || exit 2; export TL
ptyrun() { python3 "$TL/ptyrun.py" "$@"; }
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
t "not a terminal" "not a tty" 1 sh -c '"$1" < /dev/null' sh "$U"
t "a file" "not a tty" 1 sh -c ': > f; "$1" < f' sh "$U"
t "a pipe" "not a tty" 1 sh -c 'echo | "$1"' sh "$U"
# on a terminal: the name the system's tty finds for the same terminal
out=$(ptyrun 012 sh -c '"$1"; tty' sh "$U")
a=${out%%"$nl"*} b=${out#*"$nl"}
case $a in /*) ;; *) a="($a)";; esac
t "a terminal: its name, as the system's tty finds it" "$b" 0 echo "$a"
t "a terminal: status 0" "" 0 sh -c 'python3 "$TL/ptyrun.py" 012 "$1" > /dev/null' sh "$U"
t "a terminal on the input only" "$b" 0 ptyrun 0 "$U"
t "a terminal on the output only: not a tty" "not a tty" 1 sh -c 'python3 "$TL/ptyrun.py" 12 "$1" < /dev/null' sh "$U"
t "a terminal: the name is the device" "yes" 0 ptyrun 0 sh -c 'n=$("$1") && test -c "$n" && echo yes' sh "$U"
t "--" "not a tty" 1 sh -c '"$1" -- < /dev/null' sh "$U"
t "an operand" "" 2 "$U" x
t "an option" "" 2 "$U" -s
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: tty x -s: x is an operand" "" 2 "$U" x -s
[ -c /dev/full ] && t "a write error" "" 2 sh -c '"$1" < /dev/null > /dev/full' sh "$U"

done_tests tty
