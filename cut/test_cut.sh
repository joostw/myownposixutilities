#!/bin/sh
# test_cut.sh - tests for cut (POSIX.1-2024).  Run: sh test_cut.sh
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
U=${U:-$PWD/cut}
# a UTF-8 locale for the multibyte tests, which are skipped without one (POSIX
# names none): $UTF8_LOCALE, or C.UTF-8, or any UTF-8 locale that locale -a lists
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
export U8
u8() { [ -n "$U8" ]; }
T=${TMPDIR:-/tmp}/test_cut.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
u8=$U8
c() { _in=$1; shift; printf '%s' "$_in" | "$U" "$@"; }
cu() { _in=$1; shift; printf "$_in" | LC_ALL=$u8 "$U" "$@"; }
t "-c rationale example" "abdefg" 0 c 'abcdefghi
' -c6,2,4-7,1
t "-c single" "c" 0 c 'abc
' -c3
t "-c -high" "ab" 0 c 'abcd
' -c-2
t "-c low-" "cd" 0 c 'abcd
' -c3-
t "-c past end" "" 0 c 'ab
' -c5
t "-c blank list" "ad" 0 c 'abcd
' -c '1 4'
t "-c repeated once" "a" 0 c 'abc
' -c1,1,1-1
u8 && t "-c UTF-8 chars" "é" 0 cu 'aéb\n' -c2
u8 && t "-c UTF-8 range" "éb" 0 cu 'aébc\n' -c2-3
u8 && t "-b bytes" "$(printf '\303')" 0 cu 'aéb\n' -b2
u8 && t "-b -n keeps chars" "a" 0 cu 'aéb\n' -nb1-2
u8 && t "-b -n single at 2nd byte" "é" 0 cu 'aéb\n' -nb3
u8 && t "-b -n single at 1st byte dropped" "" 0 cu 'éb\n' -nb1
u8 && t "-b -n low-" "éb" 0 cu 'aéb\n' -nb3-
u8 && t "-b -n -high" "aé" 0 cu 'aéb\n' -nb-3
t "-f default tab" "b" 0 c 'a	b	c
' -f2
t "-f -d" "a:c" 0 c 'a:b:c
' -d: -f1,3
t "-f low-" "b:c" 0 c 'a:b:c
' -d: -f2-
t "-f no delim passes" "abc" 0 c 'abc
' -d: -f2
t "-f -s suppresses" "" 0 c 'abc
' -d: -s -f2
t "-f past end" "" 0 c 'a:b
' -d: -f5
t "-f empty fields" ":" 0 c '::
' -d: -f1-2
u8 && t "-f multibyte delim" "b€c" 0 cu 'a€b€c\n' -d€ -f2-
t "-f order of input" "a:c" 0 c 'a:b:c
' -d: -f3,1
t "no final newline" "ab" 0 c 'abc' -c1-2
t "-f many lines" "1
2
3" 0 c '1:a
2:b
3:c
' -d: -f1
t "files and -" "x
y
z" 0 sh -c 'printf "x\n" > "$2/f1"; printf "z\n" > "$2/f2"; printf "y\n" | "$1" -c1 "$2/f1" - "$2/f2"' sh "$U" "$T"
t "missing file continues" "x" 1 sh -c '"$1" -c1 "$2/nope" "$2/f1"' sh "$U" "$T"
t "zero in list" "" 1 c 'a
' -c0
t "decreasing range" "" 1 c 'a
' -c3-1
t "garbage list" "" 1 c 'a
' -c1x
t "empty list" "" 1 c 'a
' -c ''
t "two modes" "" 1 c 'a
' -c1 -b1
t "no mode" "" 1 c 'a
'
t "two-char delim" "" 1 c 'a
' -d ab -f1
t "long line" "65537" 0 sh -c 'awk "BEGIN { while (i++ < 100000) printf \"x\"; print \"\" }" | "$1" -c-65536 | wc -c | tr -d " "' sh "$U"
t "huge number" "a" 0 c 'a
' -c1-99999999999999999999
[ -c /dev/full ] && t "write error" "" 1 sh -c 'echo abc | "$1" -c1 >/dev/full' sh "$U"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: cut -c1 f -s: -s is a file" "x" 1 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" -c1 f -s; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests cut
