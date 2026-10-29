#!/bin/sh
# test_pwd.sh - tests for pwd (POSIX.1-2024).  Run: sh test_pwd.sh
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
U=${U:-$PWD/pwd}
d=${TMPDIR:-/tmp}/test_pwd.$$; mkdir -m 700 "$d" || exit 2; trap 'rm -rf "$d"' EXIT; d=$(cd "$d" && $U -P); mkdir "$d/real"; ln -s real "$d/link"
t '-P resolves links' "$d/real" 0 sh -c "cd '$d/link' && $U -P"
t '-L uses PWD' "$d/link" 0 sh -c "cd '$d/link' && PWD='$d/link' $U -L"
t 'default is -L' "$d/link" 0 sh -c "cd '$d/link' && PWD='$d/link' $U"
t 'last option wins (-L -P)' "$d/real" 0 sh -c "cd '$d/link' && PWD='$d/link' $U -L -P"
t 'last option wins (-P -L)' "$d/link" 0 sh -c "cd '$d/link' && PWD='$d/link' $U -P -L"
t 'PWD with dot ignored' "$d/real" 0 sh -c "cd '$d/link' && PWD='$d/./link' $U"
t 'PWD with dot-dot ignored' "$d/real" 0 sh -c "cd '$d/link' && PWD='$d/real/../link' $U"
t 'PWD of another directory' "$d/real" 0 sh -c "cd '$d/link' && PWD='$d' $U"
t 'relative PWD ignored' "$d/real" 0 sh -c "cd '$d/link' && PWD='link' $U"
t 'unknown option' '' 1 $U -z
[ -c /dev/full ] && t 'write error' '' 1 sh -c "$U >/dev/full"
# deeper than PATH_MAX: entered one level at a time with cd -P
ok 'deeper than PATH_MAX' sh -c '
    cd "$1" || exit 1
    n=$(printf "%0100d" 0 | tr 0 d) i=0
    while [ $i -lt 50 ]; do mkdir $n && cd -P $n || exit 1; i=$((i + 1)); done
    out=$("$2" -P) || exit 1
    [ ${#out} -gt 4096 ] || exit 1
    case $out in */$n) exit 0;; esac
    exit 1' sh "$d/real" "$U"
rm -rf "$d"
done_tests pwd
