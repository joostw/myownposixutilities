#!/bin/sh
# test_link.sh - tests for link (POSIX.1-2024).  Run: sh test_link.sh
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
U=${U:-$PWD/link}
T=${TMPDIR:-/tmp}/test_link.$$
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
echo x > f
t "a link" "" 0 "$U" f g
t "the same file" "2" 0 fstat %h f
t "the contents" "x" 0 cat g
t "exists" "" 1 "$U" f g
t "a missing file" "" 1 "$U" nofile h
t "a directory" "" 1 sh -c 'mkdir d && "$1" d dl' sh "$U"
t "a symbolic link: the link itself" "" 0 sh -c 'ln -s f s && "$1" s sl && test -h sl' sh "$U"
t "one operand" "" 1 "$U" f
t "three operands" "" 1 "$U" f a b
t "--" "" 0 sh -c '"$1" -- f -x && test -f ./-x' sh "$U"
t "an option" "" 1 "$U" -x f y

done_tests link
