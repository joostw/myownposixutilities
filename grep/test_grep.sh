#!/bin/sh
# test_grep.sh - tests for grep (POSIX.1-2024).  Run: sh test_grep.sh
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
U=${U:-$PWD/grep}
# a UTF-8 locale for the multibyte tests, which are skipped without one (POSIX
# names none): $UTF8_LOCALE, or C.UTF-8, or any UTF-8 locale that locale -a lists
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
export U8
u8() { [ -n "$U8" ]; }
T=${TMPDIR:-/tmp}/test_grep.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 2
printf 'apple\nBanana\ncherry\napple pie\n' > f
printf 'one\ntwo\n' > g
printf 'apple\n\n' > pats
: > empty
t "BRE" "apple
apple pie" 0 "$U" 'ap*le' f
t "BRE interval" "apple
apple pie" 0 "$U" 'p\{2\}' f
t "BRE + literal" "" 1 "$U" 'p+' f
t "ERE +" "apple
apple pie" 0 "$U" -E 'p+l' f
t "ERE alternation" "Banana
cherry" 0 "$U" -E 'nan|err' f
t "-F literal" "" 1 "$U" -F 'a.p' f
t "-F" "apple pie" 0 "$U" -F 'e p' f
t "no match status" "" 1 "$U" zzz f
t "-c" "2" 0 "$U" -c apple f
t "-c zero" "0" 1 "$U" -c zzz f
t "-v" "Banana
cherry" 0 "$U" -v apple f
t "-vc" "2" 0 "$U" -vc apple f
t "-i" "Banana" 0 "$U" -i banana f
t "-i -F" "Banana" 0 "$U" -iF BANANA f
t "-x" "apple" 0 "$U" -x apple f
t "-x ERE alt" "apple" 0 "$U" -xE 'app|apple' f
t "-x -F" "apple" 0 "$U" -xF apple f
t "-n" "1:apple
4:apple pie" 0 "$U" -n apple f
t "two files prefix" "f:apple
f:apple pie
g:one" 0 "$U" -e apple -e one f g
t "-c two files" "f:2
g:0" 0 "$U" -c apple f g
t "-l" "f" 0 "$U" -l apple f g
t "-l before -c wins" "f" 0 "$U" -l -c apple f g
t "-c before -l, -l wins" "f" 0 "$U" -c -l apple f g
t "-q" "" 0 "$U" -q apple f g
t "-q with -l" "" 0 "$U" -lq apple f g
t "-q no match" "" 1 "$U" -q zzz f
t "-q error but match" "" 0 "$U" -q apple nofile f
t "error status" "f:apple
f:apple pie" 2 "$U" apple nofile f
t "-s quiet missing" "" 2 sh -c '"$1" -s apple nofile 2>&1' sh "$U"
t "-s directory" "" 2 sh -c 'mkdir -p dir; "$1" -s apple dir 2>&1' sh "$U"
t "-e multiple" "apple
cherry
apple pie" 0 "$U" -e apple -e cherry f
t "newline list" "apple
cherry
apple pie" 0 "$U" 'apple
cherry' f
t "trailing newline = null pattern" "4" 0 "$U" -c 'zzz
' f
t "adjacent newlines" "4" 0 "$U" -c 'zzz

yyy' f
t "-e ''" "4" 0 "$U" -c -e '' f
t "-E null" "4" 0 "$U" -Ec '' f
t "-f file with empty line" "4" 0 "$U" -c -f pats f
t "-f empty file" "0" 1 "$U" -c -f empty f
t "-f missing" "" 2 "$U" -f nopats f
t "-f and -e" "cherry
apple pie" 0 "$U" -e cherry -e pie -f empty f
t "stdin" "two" 0 sh -c 'printf "one\ntwo\n" | "$1" tw' sh "$U"
t "stdin -l" "(standard input)" 0 sh -c 'printf "one\ntwo\n" | "$1" -l tw' sh "$U"
t "- operand" "(standard input):two
g:two" 0 sh -c 'printf "two\n" | "$1" tw - g' sh "$U"
tb "no final newline" 'a b c \n' sh -c 'printf "abc" | "$1" b' sh "$U"
t "bad RE" "" 2 "$U" 'a\{1' f
t "no pattern" "" 2 "$U"
t "bad option" "" 2 "$U" -y a f
t "-- pattern" "-x" 0 sh -c 'printf -- "-x\n" | "$1" -- -x' sh "$U"
t "bracket" "cherry" 0 "$U" '[h]e' f
t "anchors" "Banana" 0 "$U" '^B.*a$' f
t "backref" "Banana" 0 "$U" '\(an\)\1' f
u8 && t "UTF-8 -i -F" "Éclair" 0 sh -c 'printf "Éclair\n" | LC_ALL=$U8 "$1" -iF éclair' sh "$U"
u8 && t "UTF-8 -i" "Éclair" 0 sh -c 'printf "Éclair\n" | LC_ALL=$U8 "$1" -i éclair' sh "$U"
u8 && t "UTF-8 . one char" "é" 0 sh -c 'printf "é\n" | LC_ALL=$U8 "$1" -x .' sh "$U"
u8 && t "-iF invalid line falls back" "$(printf 'A\377')" 0 sh -c 'printf "A\377\n" | LC_ALL=$U8 "$1" -iF a' sh "$U"
[ -c /dev/full ] && t "write error" "" 2 sh -c '"$1" apple f >/dev/full' sh "$U"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: grep x f -v: -v is a file" "f:x" 2 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" x f -v; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests grep
