#!/bin/sh
# test_chgrp.sh - tests for chgrp (POSIX.1-2024).  Run: sh test_chgrp.sh
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
U=${U:-$PWD/chgrp}
T=${TMPDIR:-/tmp}/test_chgrp.$$
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
me=$(id -u); mg=$(id -g); gn=$(id -gn)
: > f; mkdir -p d/e; : > d/e/g; ln -s f lf; ln -s d ld; ln -s nowhere dl
t "own group by number" "$mg" 0 sh -c '"$1" "$2" f && "$3" %g f' sh "$U" "$mg" "$T.fstat"
t "own group by name" "$mg" 0 sh -c '"$1" "$2" f && "$3" %g f' sh "$U" "$gn" "$T.fstat"
t "an unknown name" "" 1 "$U" no_such_group_here f
t "an empty group" "" 2 "$U" "" f
t "a missing file" "" 1 "$U" "$mg" nofile
t "no file" "" 2 "$U" "$mg"
t "a bad option" "" 2 "$U" -x "$mg" f
if $root; then
    t "a group; the owner unchanged" "0 5678" 0 sh -c '"$1" 5678 f && "$2" "%u %g" f' sh "$U" "$T.fstat"
    t "a link operand: its target" "11" 0 sh -c '"$1" 11 lf && "$2" %g f' sh "$U" "$T.fstat"
    t "-h: the link itself" "12 11" 0 sh -c '"$1" -h 12 lf && echo $("$2" %g lf) $("$2" %g f)' sh "$U" "$T.fstat"
    t "-R" "21 21 21" 0 sh -c '"$1" -R 21 d && echo $("$2" %g d) $("$2" %g d/e) $("$2" %g d/e/g)' sh "$U" "$T.fstat"
    t "-R -H: the operand followed" "24 24" 0 sh -c '"$1" -R -H 24 ld && echo $("$2" %g d) $("$2" %g d/e/g)' sh "$U" "$T.fstat"
    t "-R -L: links below followed" "26" 0 sh -c 'ln -s ../f d/lnk && "$1" -R -L 26 d && "$2" %g f' sh "$U" "$T.fstat"
    t "-R -P: links below changed themselves" "27 26" 0 sh -c '"$1" -R -P 27 d && echo $("$2" %g d/lnk) $("$2" %g f)' sh "$U" "$T.fstat"
else
    t "a group one is not in: refused" "" 1 "$U" 0 f
fi
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: chgrp g f -R: -R is a file" "" 1 "$U" "$mg" f -R

done_tests chgrp
