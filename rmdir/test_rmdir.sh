#!/bin/sh
# test_rmdir.sh - tests for rmdir (POSIX.1-2024).  Run: sh test_rmdir.sh
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
U=${U:-$PWD/rmdir}
T=${TMPDIR:-/tmp}/test_rmdir.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
mkdir d
t "a directory" "" 0 "$U" d
ok "gone" test ! -e d
t "missing" "" 1 "$U" nodir
t "not empty" "" 1 sh -c 'mkdir -p e/f && "$1" e' sh "$U"
ok "not empty: kept" test -d e/f
t "a file" "" 1 sh -c ': > fl && "$1" fl' sh "$U"
t "in order: child first" "" 0 sh -c 'mkdir -p o/p && "$1" o/p o && test ! -e o' sh "$U"
t "in order: parent first fails" "" 1 sh -c 'mkdir -p q/r && "$1" q q/r' sh "$U"
ok "parent first: the child is gone, the parent stays" test -d q -a ! -e q/r
mkdir -p a/b/c
t "-p" "" 0 "$U" -p a/b/c
ok "-p all gone" test ! -e a
mkdir -p s/t/u; : > s/keep
t "-p stops at a directory that is not empty" "" 1 "$U" -p s/t/u
ok "-p: s stays, t and u are gone" test -d s -a ! -e s/t
mkdir -p x//y///z
t "-p with doubled and trailing slashes" "" 0 "$U" -p x//y///z/
ok "-p slashes: all gone" test ! -e x
t "-p an absolute path: up to a directory that is not empty" "" 1 sh -c 'mkdir -p "$2/ab/cd" && "$1" -p "$2/ab/cd"; r=$?; test ! -e "$2/ab" && exit $r' sh "$U" "$PWD"
mkdir l
t "--" "" 0 sh -c 'mkdir ./-p && "$1" -- -p && test ! -e ./-p' sh "$U"
t "trailing slash" "" 0 "$U" l/
t "dot is refused" "" 1 "$U" .
t "no operands" "" 2 "$U"
t "bad option" "" 2 "$U" -x d
if ! $root; then
    mkdir -p ro/sub; chmod 555 ro
    t "in a read-only directory" "" 1 "$U" ro/sub
    chmod 755 ro
fi
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: rmdir d -p: -p is a directory" "" 0 sh -c 'mkdir d ./-p && "$1" d -p && test ! -e ./-p' sh "$U"

done_tests rmdir
