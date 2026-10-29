#!/bin/sh
# test_realpath.sh - tests for realpath (POSIX.1-2024).  Run: sh test_realpath.sh
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
U=${U:-$PWD/realpath}
T=${TMPDIR:-/tmp}/test_realpath.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
P=$(cd -P . && pwd)
mkdir -p d/e; : > d/f; ln -s d ld; ln -s nowhere dl; ln -s ../f d/e/lf; ln -s loop loop
t "a directory" "$P/d" 0 "$U" d
t "dot and dot-dot" "$P/d" 0 "$U" ./d/e/../.
t "a link" "$P/d" 0 "$U" ld
t "through a link" "$P/d/f" 0 "$U" ld/e/lf
t "an absolute path" "/" 0 "$U" /
t "a missing last component" "$P/d/new" 0 "$U" d/new
t "-E a missing last component" "$P/d/new" 0 "$U" -E d/new
t "-e a missing last component" "" 1 "$U" -e d/new
t "-e existing" "$P/d/f" 0 "$U" -e d/f
t "-E -e: the last" "" 1 "$U" -E -e d/new
t "-e -E: the last" "$P/d/new" 0 "$U" -e -E d/new
t "a missing directory" "" 1 "$U" nodir/new
t "-E a dangling link: its target" "$P/nowhere" 0 "$U" -E dl
t "-e a dangling link" "" 1 "$U" -e dl
t "-E trailing slashes are ignored" "$P/d/new" 0 "$U" -E d/new//
t "-E a single component" "$P/new" 0 "$U" -E new
t "a file as a directory" "" 1 "$U" d/f/x
t "a loop of links" "" 1 "$U" loop
t "an empty operand" "" 1 "$U" ""
t "no operand" "" 2 "$U"
t "a bad option" "" 2 "$U" -x d
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: realpath d -e: two operands" "" 2 "$U" d -e

done_tests realpath
