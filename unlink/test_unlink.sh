#!/bin/sh
# test_unlink.sh - tests for unlink (POSIX.1-2024).  Run: sh test_unlink.sh
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
U=${U:-$PWD/unlink}
d=${TMPDIR:-/tmp}/test_unlink.$$; mkdir -m 700 "$d" || exit 2; trap 'rm -rf "$d"' EXIT; : > "$d/f"; mkdir "$d/dir"
t 'removes a file' '' 0 $U "$d/f"
ok 'file is gone' test ! -e "$d/f"
t 'missing file' '' 1 $U "$d/f"
t 'directory' '' 1 $U "$d/dir"
: > "$d/-x"
t '-- before an operand' '' 0 $U -- "$d/-x"
t 'no operand' '' 1 $U
t 'two operands' '' 1 $U a b
t 'unknown option' '' 1 $U -z f
rm -rf "$d"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: unlink f -x: two operands" "f" 1 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" f -x; r=$?; ls; exit $r; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests unlink
