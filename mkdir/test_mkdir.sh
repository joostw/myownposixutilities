#!/bin/sh
# test_mkdir.sh - tests for mkdir (POSIX.1-2024).  Run: sh test_mkdir.sh
cd "$(dirname "$0")" || exit 2
pass=0 fail=0
# t NAME EXPECTED-STDOUT EXPECTED-STATUS COMMAND...  (stdout compared without trailing newlines)
t() {
    _n=$1 _e=$2 _s=$3; shift 3
    _o=$("$@" 2>/dev/null); _r=$?
    if [ "$_o" = "$_e" ] && [ "$_r" = "$_s" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s] status %s\n  want: [%s] status %s\n' "$_n" "$_o" "$_r" "$_e" "$_s"; fi
}
# tb NAME EXPECTED-OD-C COMMAND...  (exact bytes of stdout, as od -c)
tb() {
    _n=$1 _e=$2; shift 2
    _o=$("$@" 2>/dev/null | od -An -c | tr -s ' \n' ' ' | sed 's/^ //;s/ $//')
    if [ "$_o" = "$_e" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  %s\n  want: %s\n' "$_n" "$_o" "$_e"; fi
}
# ok NAME CONDITION-COMMAND...
ok() {
    _n=$1; shift
    if "$@"; then pass=$((pass+1)); else fail=$((fail+1)); printf 'FAIL %s\n' "$_n"; fi
}
done_tests() { echo "$1: $pass passed, $fail failed"; [ "$fail" = 0 ]; }
U=${U:-$PWD/mkdir}
T=${TMPDIR:-/tmp}/test_mkdir.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 2
umask 022
perm() { ls -ld "$1" | cut -c1-10; }
t "simple" "" 0 "$U" a
t "mode 755" "drwxr-xr-x" 0 perm a
t "exists" "" 1 "$U" a
t "missing parent" "" 1 "$U" x/y
t "-p" "" 0 "$U" -p x/y/z
ok "-p made" test -d x/y/z
t "-p existing ok" "" 0 "$U" -p x/y/z
t "-p over file" "" 1 sh -c ': > file; "$1" -p file/sub' sh "$U"
t "-m octal" "" 0 "$U" -m 700 b
t "-m 700" "drwx------" 0 perm b
t "-m ignores umask" "" 0 sh -c 'umask 077; "$1" -m 777 c' sh "$U"
t "-m 777" "drwxrwxrwx" 0 perm c
t "-m symbolic" "" 0 "$U" -m u=rwx,g=rx,o= d
t "-m u=rwx,g=rx" "drwxr-x---" 0 perm d
t "-m a+w (adds to 777)" "" 0 "$U" -m a-w e
t "-m a-w" "dr-xr-xr-x" 0 perm e
t "-m -w no who: umask bits unaffected" "" 0 sh -c 'umask 022; "$1" -m -w f' sh "$U"
t "-m -w result" "dr-xrwxrwx" 0 perm f
t "-m =r no who" "" 0 sh -c 'umask 022; "$1" -m =r g' sh "$U"
t "-m =r result" "dr--r--r--" 0 perm g
t "-p -m only last" "" 0 "$U" -p -m 700 p/q
t "-p intermediate" "drwxr-xr-x" 0 perm p
t "-p last" "drwx------" 0 perm p/q
t "-p intermediate u+wx" "" 0 sh -c 'umask 777; "$1" -p r/s' sh "$U"
t "-p intermediate under umask 777" "d-wx------" 0 perm r
t "bad mode" "" 1 "$U" -m 999 h
ok "bad mode made nothing" test ! -e h
t "continues after error" "" 1 "$U" a i
ok "i made" test -d i
t "no operand" "" 1 "$U"
t "trailing slash" "" 0 "$U" j/
ok "j" test -d j
t "-p dot-dot" "" 0 "$U" -p k/../l
ok "l" test -d l
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: mkdir d -p: -p is a directory" "-p
d
f" 0 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" d -p && ls; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests mkdir
