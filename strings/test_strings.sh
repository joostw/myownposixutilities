#!/bin/sh
# test_strings.sh - tests for strings (POSIX.1-2024).  Run: sh test_strings.sh
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
U=${U:-$PWD/strings}
T=${TMPDIR:-/tmp}/test_strings.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
u8() { [ -n "$U8" ]; }
printf 'abc\0defg\001hijklm\nno\npqrs' > f
t "runs of 4 or more" "defg${nl}hijklm${nl}pqrs" 0 "$U" f
t "-n 3" "abc${nl}defg${nl}hijklm${nl}pqrs" 0 "$U" -n 3 f
t "-n 6" "hijklm" 0 "$U" -n 6 f
t "-n 1" "abc${nl}defg${nl}hijklm${nl}no${nl}pqrs" 0 "$U" -n 1 f
t "-t d: the byte offsets" "4 defg${nl}9 hijklm${nl}19 pqrs" 0 "$U" -t d f
t "-t o" "4 defg${nl}11 hijklm${nl}23 pqrs" 0 "$U" -t o f
t "-t x" "4 defg${nl}9 hijklm${nl}13 pqrs" 0 "$U" -t x f
t "-a: the same" "defg${nl}hijklm${nl}pqrs" 0 "$U" -a f
t "the standard input" "defg${nl}hijklm${nl}pqrs" 0 sh -c '"$1" < f' sh "$U"
t "a string at the end of the file" "xyzw" 0 sh -c 'printf "\001\001xyzw" | "$1"' sh "$U"
t "a tab ends a string" "abcd${nl}efgh" 0 sh -c 'printf "abcd\tefgh\n" | "$1"' sh "$U"
t "spaces are printable" "a b c d" 0 sh -c 'printf "\001a b c d\001" | "$1"' sh "$U"
t "bytes above 127 are not printable in the C locale" "abcd${nl}efgh" 0 sh -c 'printf "abcd\351efgh" | "$1"' sh "$U"
t "a long string" "5000" 0 sh -c 'awk "BEGIN { while (i++ < 5000) printf \"x\" }" | "$1" | wc -c | awk "{ print \$1 - 1 }"' sh "$U"
t "a string across the reads" "yes" 0 sh -c 'awk "BEGIN { while (i++ < 70000) printf \"y\"; printf \"\\001\" }" | "$1" | awk "length(\$0) == 70000 { print \"yes\" }"' sh "$U"
t "several files: offsets from each start" "4 defg${nl}9 hijklm${nl}19 pqrs${nl}4 defg${nl}9 hijklm${nl}19 pqrs" 0 "$U" -t d f f
t "a binary: as the system's strings finds its strings without tabs" "same" 0 sh -c 'tr "\t" "\001" < "$1" > bin; a=$("$2" -n 6 bin); b=$(strings -a -n 6 bin); [ "$a" = "$b" ] && echo same' sh "$U" "$U"
u8 && t "UTF-8: a multibyte character is one" "été!" 0 sh -c 'printf "\001été!\001ab\001" | LC_ALL=$2 "$1"' sh "$U" "$U8"
u8 && t "UTF-8: -n counts characters" "" 0 sh -c 'printf "\001éé\001" | LC_ALL=$2 "$1" -n 3' sh "$U" "$U8"
u8 && t "UTF-8: a bad sequence ends a string" "abcd${nl}efgh" 0 sh -c 'printf "abcd\377efgh" | LC_ALL=$2 "$1"' sh "$U" "$U8"
u8 && t "UTF-8: -t d counts bytes" "3 ééxy" 0 sh -c 'printf "\001\001\001ééxy" | LC_ALL=$2 "$1" -t d' sh "$U" "$U8"
t "a missing file: the others still" "defg${nl}hijklm${nl}pqrs" 1 "$U" no_such_file f
t "-t z" "" 2 "$U" -t z f
t "-n 0" "" 2 "$U" -n 0 f
t "-n x" "" 2 "$U" -n x f
t "an unknown option" "" 2 "$U" -x f
t "--" "defg${nl}hijklm${nl}pqrs" 0 "$U" -- f
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: strings f -a: -a is a file" "defg${nl}hijklm${nl}pqrs" 1 "$U" f -a
[ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" f > /dev/full' sh "$U"

done_tests strings
