#!/bin/sh
# test_tr.sh - tests for tr (POSIX.1-2024).  Run: sh test_tr.sh
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
U=${U:-$PWD/tr}
# a UTF-8 locale for the multibyte tests, which are skipped without one (POSIX
# names none): $UTF8_LOCALE, or C.UTF-8, or any UTF-8 locale that locale -a lists
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
export U8
u8() { [ -n "$U8" ]; }
T=${TMPDIR:-/tmp}/test_tr.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
u8=$U8
r() { _in=$1; shift; printf "$_in" | LC_ALL=C "$U" "$@"; }
ru() { _in=$1; shift; printf "$_in" | LC_ALL=$u8 "$U" "$@"; }
t "simple" "hEllo" 0 r 'hello' e E
t "range" "HELLO" 0 r 'hello' a-z A-Z
t "class case" "HELLO" 0 r 'hello' '[:lower:]' '[:upper:]'
t "class case down" "hello" 0 r 'HELLO' '[:upper:]' '[:lower:]'
t "case at position" "2HELLO" 0 r '1hello' '1[:lower:]' '2[:upper:]'
t "case misplaced" "" 1 r 'hello' 'ab[:lower:]' '[:upper:]'
t "other class in s2" "" 1 r 'hello' 'a' '[:digit:]'
t "pad with last (BSD)" "ddd" 0 r '123' 0-9 d
t "[x*] fill" "ddd" 0 r '123' 0-9 '[d*]'
t "[x*0] fill" "ddd" 0 r '123' 0-9 '[d*0]'
t "[x*n] decimal" "aaaaaaaaaab" 0 r 'abcdefghijk' a-k '[a*10]b'
t "[x*n] octal" "aaaaaaaab" 0 r 'abcdefghi' a-i '[a*010]b'
t "fill in middle" "xyyyz" 0 r 'abcde' a-e 'x[y*]z'
t "[x*n] in string1" "" 1 r 'a' '[a*2]' b
t "escapes" "a b" 0 r 'a\tb' '\t' ' '
t "octal" "aXb" 0 r 'a\001b' '\001' X
t "octal nul" "aXb" 0 r 'a\000b' '\000' X
t "delete nul" "ab" 0 r 'a\000b' -d '\000'
t "backslash" "a/b" 0 r 'a\\b' '\\' /
t "-d" "hll" 0 r 'hello' -d aeiou
t "-d class" "abc" 0 r 'a1b2c3' -d '[:digit:]'
t "-c -d" "123" 0 r 'a1b2c3' -cd '[:digit:]'
t "-C -d" "123" 0 r 'a1b2c3' -Cd '[:digit:]'
t "-s" "abc" 0 r 'aaabbbccc' -s abc
t "-s only listed" "abbc" 0 r 'aabbc' -s a
t "-s class" "a b c" 0 r 'a   b  c' -s '[:space:]'
t "translate+squeeze" "x-y" 0 r 'a---b' -s 'ab-' 'xy-'
t "squeeze uses string2" "xy" 0 r 'aabb' -s ab xy
t "squeeze case conv" "AB" 0 r 'aabb' -s '[:lower:]' '[:upper:]'
t "-ds" "a-b" 0 r 'a11--b' -ds 1 -
t "-ds needs both" "" 1 r 'a' -ds 1
t "-d string2 ignored" "b" 0 r 'ab' -d a xyz
t "words (spec example)" "one
two
three" 0 r 'one, two;three\n' -cs '[:alpha:]' '[\n*]'
t "-c order binary" "AB" 0 r '\000\001' -c '\003-\377' 'AB'
t "-c translate" "a--b" 0 r 'a12b' -c 'a-z' '-'
t "-c squeeze last char" "a-b" 0 r 'a12b' -cs 'a-z' '-'
t "range reversed" "" 1 r 'a' z-a A
t "dash literal end" "a_b" 0 r 'a-b' 'x-' '_' 
t "dash literal start" "a_b" 0 r 'a-b' -- '-x' '_x'
t "brackets literal (SysV)" "HELLO" 0 r 'hello' '[a-z]' '[A-Z]'
t "repeated char last wins" "y" 0 r 'a' aa xy
t "empty string2 translate" "" 1 r 'a' a ''
t "no operands" "" 1 r 'a'
t "too many" "" 1 r 'a' a b c
t "translate needs 2" "" 1 r 'a' a
t "bad option" "" 1 r 'a' -x a b
t "bad class" "" 1 r 'a' '[:nope:]' x
t "equiv self" "xbc" 0 r 'abc' '[=a=]' x
t "high bytes C" "$(printf 'a\375')" 0 r 'a\376' '\376' '\375'
u8 && t "invalid UTF-8 bytes kept" "$(printf 'A\377B')" 0 ru 'a\377b' a-z A-Z
u8 && t "UTF-8 char" "aeb" 0 ru 'aéb' 'é' 'e'
u8 && t "UTF-8 to UTF-8" "a€b" 0 ru 'a$b' '$' '€'
u8 && t "UTF-8 octal concat" "aeb" 0 ru 'aéb' '\303\251' 'e'
u8 && t "UTF-8 octal raw" "$(printf 'aXb')" 0 ru 'a\377b' '\377' 'X'
u8 && t "UTF-8 case" "ÉCOLE" 0 ru 'école' '[:lower:]' '[:upper:]'
u8 && t "UTF-8 delete" "ab" 0 ru 'aéb' -d 'é'
u8 && t "UTF-8 -c delete" "éé" 0 ru 'aébé' -cd 'é'
u8 && t "UTF-8 -cs" "a b" 0 ru 'a€€b' -cs 'a-z' ' '
u8 && t "UTF-8 squeeze" "aéb" 0 ru 'aéééb' -s 'é'
u8 && t "UTF-8 range" "a**b" 0 ru 'aéêb' 'é-ê' '*'
u8 && t "UTF-8 class alpha" "a1" 0 ru 'aé1' -d 'é'
u8 && t "UTF-8 -C order" "AB" 0 ru '\000\001' -C '\003-\177' 'AB'
t "large input" "200000" 0 sh -c 'awk "BEGIN { while (i++ < 100000) printf \"ab\" }" | "$1" a b | tr -d a | wc -c | tr -d " "' sh "$U"
[ -c /dev/full ] && t "write error" "" 1 sh -c 'echo abc | "$1" a b >/dev/full' sh "$U"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: tr a b -d: three operands" "" 1 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { echo ab | "$1" a b -d; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests tr
