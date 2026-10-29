#!/bin/sh
# test_chown.sh - tests for chown (POSIX.1-2024).  Run: sh test_chown.sh
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
U=${U:-$PWD/chown}
T=${TMPDIR:-/tmp}/test_chown.$$
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
me=$(id -u); mg=$(id -g); mn=$(id -un)
ids() { echo $(fstat %u "$1") $(fstat %g "$1"); }
: > f; mkdir -p d/e; : > d/e/g; ln -s f lf; ln -s d ld; ln -s nowhere dl
t "own user by number" "$me $mg" 0 sh -c '"$1" "$2" f && "$3" "%u %g" f' sh "$U" "$me" "$T.fstat"
t "own user by name" "$me $mg" 0 sh -c '"$1" "$2" f && "$3" "%u %g" f' sh "$U" "$mn" "$T.fstat"
t "an unknown name" "" 1 "$U" no_such_user_here f
t "an empty owner" "" 2 "$U" ":$mg" f
t "an empty group" "" 2 "$U" "$me:" f
t "a missing file" "" 1 "$U" "$me" nofile
t "no file" "" 2 "$U" "$me"
t "a bad option" "" 2 "$U" -x "$me" f
if $root; then
    t "user and group" "1234 5678" 0 sh -c '"$1" 1234:5678 f && "$2" "%u %g" f' sh "$U" "$T.fstat"
    t "user only: the group unchanged" "4321 5678" 0 sh -c '"$1" 4321 f && "$2" "%u %g" f' sh "$U" "$T.fstat"
    t "a link operand: its target" "11 5678" 0 sh -c '"$1" 11 lf && "$2" "%u %g" f' sh "$U" "$T.fstat"
    t "-h: the link itself" "12 0 11" 0 sh -c '"$1" -h 12:0 lf && echo $("$2" "%u %g" lf) $("$2" %u f)' sh "$U" "$T.fstat"
    t "-h a dangling link" "13" 0 sh -c '"$1" -h 13 dl && "$2" %u dl' sh "$U" "$T.fstat"
    t "a dangling link without -h" "" 1 "$U" 14 dl
    t "-R" "21 21 21" 0 sh -c '"$1" -R 21 d && echo $("$2" %u d) $("$2" %u d/e) $("$2" %u d/e/g)' sh "$U" "$T.fstat"
    t "-R: -P is the default, links are changed, not followed" "22 11" 0 sh -c 'ln -s ../f d/lnk && "$1" -R 22 d && echo $("$2" %u d/lnk) $("$2" %u f)' sh "$U" "$T.fstat"
    t "-R -P: the operand link itself" "23 22" 0 sh -c '"$1" -R -P 23 ld && echo $("$2" %u ld) $("$2" %u d)' sh "$U" "$T.fstat"
    t "-R -H: the operand followed" "24 24" 0 sh -c '"$1" -R -H 24 ld && echo $("$2" %u d) $("$2" %u d/e/g)' sh "$U" "$T.fstat"
    t "-R -H: links below not followed" "11" 0 sh -c '"$1" -R -H 25 ld && "$2" %u f' sh "$U" "$T.fstat"
    t "-R -L: links below followed" "26" 0 sh -c '"$1" -R -L 26 d && "$2" %u f' sh "$U" "$T.fstat"
    t "set-user-ID cleared on a regular file, or not: chown() decides" "" 0 sh -c 'chmod 4755 f && "$1" 30 f' sh "$U"
    ln -s .. d/e/up
    t "-R -L a loop: reported, status 1" "" 1 "$U" -R -L 27 d
    rm d/e/up
    name=$(awk 'BEGIN { while (i++ < 100) printf "n" }')
    mkdir deep
    (cd -P deep && i=0 && while [ $i -lt 60 ]; do mkdir "$name" && cd -P "$name" || exit 1; i=$((i+1)); done; : > leaf)
    t "-R deeper than PATH_MAX, 16 open files" "40" 0 sh -c 'ulimit -n 16 && "$1" -R 40 deep && cd -P deep && while [ -d "$2" ]; do cd -P "$2"; done; "$3" %u leaf' sh "$U" "$name" "$T.fstat"
else
    t "another user: refused" "" 1 "$U" 0 f
    t "-R own user" "$me" 0 sh -c '"$1" -R "$2" d && "$3" %u d/e/g' sh "$U" "$me" "$T.fstat"
fi
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: chown me f -R: -R is a file" "" 1 "$U" "$me" f -R

done_tests chown
