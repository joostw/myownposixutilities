#!/bin/sh
# test_pathchk.sh - tests for pathchk (POSIX.1-2024).  Run: sh test_pathchk.sh
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
U=${U:-$PWD/pathchk}
T=${TMPDIR:-/tmp}/test_pathchk.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
mkdir -p d; : > f
n255=$(awk 'BEGIN { while (i++ < 255) printf "n" }')
n256=$(awk 'BEGIN { while (i++ < 256) printf "n" }')
t "fine" "" 0 "$U" d/new
t "several fine" "" 0 "$U" d f d/new/deeper
t "a missing component may be created" "" 0 "$U" nodir/x
t "a component of NAME_MAX" "" 0 "$U" "d/$n255"
t "a component longer than NAME_MAX" "" 1 "$U" "d/$n256"
t "longer than NAME_MAX, missing directories" "" 1 "$U" "x/y/$n256"
long=$(awk 'BEGIN { while (i++ < 2100) printf "ab/" }')
t "longer than PATH_MAX" "" 1 "$U" "$long"
t "a file as a directory" "" 1 "$U" f/x
t "the message names the pathname" "1" 0 sh -c '"$1" f/x 2>&1 | grep -c "f/x"' sh "$U"
t "one bad among good" "" 1 "$U" d f/x d/y
t "an empty pathname" "" 1 "$U" ""
t "-p fine" "" 0 "$U" -p a/b_c.d-1
t "-p a character outside the set" "" 1 "$U" -p "a b"
t "-p a character outside ASCII" "" 1 "$U" -p "$(printf 'caf\303\251')"
t "-p a component of 14" "" 0 "$U" -p abcdefghijklmn
t "-p a component of 15" "" 1 "$U" -p abcdefghijklmno
t "-p 255 bytes" "" 0 "$U" -p "$(awk 'BEGIN { while (i++ < 85) printf "ab/" }')"
t "-p 256 bytes" "" 1 "$U" -p "$(awk 'BEGIN { while (i++ < 85) printf "ab/" }')a"
t "-p does not look at the file system" "" 0 "$U" -p f/x
t "-P a leading hyphen" "" 1 "$U" -P a/-b
t "-P a leading hyphen, first" "" 1 "$U" -P -- -a
t "-P a hyphen inside" "" 0 "$U" -P a/b-c
t "-P an empty pathname" "" 1 "$U" -P ""
t "-P also the file system checks" "" 1 "$U" -P f/x
t "-p -P" "" 1 "$U" -p -P a/-b
if ! $root; then
    mkdir -p ns/in; chmod 0 ns
    t "a directory that cannot be searched" "" 1 "$U" ns/in/x
    chmod 755 ns
fi
t "no operands" "" 2 "$U"
t "a bad option" "" 2 "$U" -x a
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: pathchk a -p: -p is a pathname" "" 0 "$U" a -p

done_tests pathchk
