#!/bin/sh
# test_mkfifo.sh - tests for mkfifo (POSIX.1-2024).  Run: sh test_mkfifo.sh
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
U=${U:-$PWD/mkfifo}
T=${TMPDIR:-/tmp}/test_mkfifo.$$
mkdir -m 700 "$T" || exit 2
# fstat: exact file status, which POSIX ls does not show (../testlib/fstat.c)
cc=${CC:-}
[ -n "$cc" ] || for c in c17 c99 cc; do command -v $c >/dev/null 2>&1 && { cc=$c; break; }; done
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T" "$T.fstat"' EXIT
$cc -o "$T.fstat" ../testlib/fstat.c || { echo "cannot compile ../testlib/fstat.c"; exit 2; }
fstat() { "$T.fstat" "$@"; }
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
t "a FIFO" "" 0 "$U" p
ok "it is a FIFO" test -p p
t "the mode: rw for all, less the umask" "640" 0 sh -c 'umask 027 && "$1" p2 && "$2" %a p2' sh "$U" "$T.fstat"
t "several" "" 0 "$U" p3 p4
ok "several: both" test -p p3 -a -p p4
t "exists" "" 1 "$U" p
t "exists: the others made" "" 1 sh -c '"$1" p p5 && exit 0; test -p p5 && exit 1' sh "$U"
t "-m octal, the umask ignored" "604" 0 sh -c 'umask 077 && "$1" -m 604 m1 && "$2" %a m1' sh "$U" "$T.fstat"
t "-m symbolic, from a=rw" "220" 0 sh -c 'umask 022 && "$1" -m g-r,o=,u-r m2 && "$2" %a m2' sh "$U" "$T.fstat"
t "-m symbolic without who: the umask's bits stay" "466" 0 sh -c 'umask 022 && "$1" -m -w m3 && "$2" %a m3' sh "$U" "$T.fstat"
t "-m +x without who: x for all the umask allows" "755" 0 sh -c 'umask 022 && "$1" -m =rw,+x m7 && "$2" %a m7' sh "$U" "$T.fstat"
t "-m u=rwx,go=" "700" 0 sh -c '"$1" -m u=rwx,go= m4 && "$2" %a m4' sh "$U" "$T.fstat"
t "-m 0" "0" 0 sh -c '"$1" -m 0 m5 && "$2" %a m5' sh "$U" "$T.fstat"
t "-m a bad mode" "" 2 "$U" -m u+q m6
ok "a bad mode: nothing made" test ! -e m6
t "in a missing directory" "" 1 "$U" nodir/p
t "no operands" "" 2 "$U"
t "a bad option" "" 2 "$U" -x p7
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: mkfifo q -m: -m is a FIFO" "" 0 sh -c '"$1" q -m && test -p ./-m' sh "$U"

done_tests mkfifo
