#!/bin/sh
# test_nl.sh - tests for nl (POSIX.1-2024).  Run: sh test_nl.sh
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
U=${U:-$PWD/nl}
T=${TMPDIR:-/tmp}/test_nl.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# n NAME INPUT EXPECTED OPTION...: printf INPUT | nl OPTION..., tabs shown as |
n() { _n=$1 _i=$2 _e=$3; shift 3; t "$_n" "$_e" 0 sh -c 'printf "$1" | { shift; "$0" "$@"; } | tr "\t" "|"' "$U" "$_i" "$@"; }
n "the default: text lines" 'a\n\nb\n' "     1|a${nl}${nl}     2|b"
n "-b a" 'a\n\nb\n' "     1|a${nl}     2|${nl}     3|b" -b a
n "-b n" 'a\nb\n' "a${nl}b" -b n
n "-b p" 'apple\nbanana\ncherry\n' "     1|apple${nl}     2|banana${nl}cherry" -b 'p^[ab]'
n "-b p, a BRE" 'ab\naab\nb\n' "ab${nl}     1|aab${nl}b" -b 'pa\{2\}'
n "-n ln" 'x\n' "1     |x" -n ln
n "-n rz" 'x\n' "000001|x" -n rz
n "-w" 'x\n' "  1|x" -w 3
n "-n rz -w 2 a big number" 'x\n' "123|x" -n rz -w 2 -v 123
n "-s" 'x\n' "     1: x" -s ': '
n "-v -i" 'a\nb\nc\n' "    10|a${nl}    15|b${nl}    20|c" -v 10 -i 5
n "-v negative" 'a\nb\n' "    -1|a${nl}     0|b" -v -1
n "-n rz negative" 'a\n' "-00002|a" -n rz -v -2
n "-l 2: every second empty line" 'a\n\n\n\n\n\nb\n' "     1|a${nl}${nl}     2|${nl}${nl}     3|${nl}${nl}     4|b" -b a -l 2
n "sections: header and footer not numbered" '\\:\\:\\:\nh\n\\:\\:\nb\n\\:\nf\n' "${nl}h${nl}${nl}     1|b${nl}${nl}f"
n "-h a -f a: one page, numbered on" '\\:\\:\\:\nh\n\\:\\:\nb\n\\:\nf\n' "${nl}     1|h${nl}${nl}     2|b${nl}${nl}     3|f" -h a -f a
n "a new page starts again" 'a\nb\n\\:\\:\\:\n\\:\\:\nc\n' "     1|a${nl}     2|b${nl}${nl}${nl}     1|c"
n "a body after a body: a new page" 'a\n\\:\\:\nb\n' "     1|a${nl}${nl}     1|b"
n "-p: no new start" 'a\n\\:\\:\\:\n\\:\\:\nb\n' "     1|a${nl}${nl}${nl}     2|b" -p
n "-d" 'a\n####\nb\n' "     1|a${nl}${nl}     1|b" -d '##'
n "-d: other lines are text" 'a\n##\nb\n' "     1|a${nl}     2|##${nl}     3|b" -d '#'
n "-d one character: the second is :" 'a\n#:#:\nb\n' "     1|a${nl}${nl}     1|b" -d '#'
n "a delimiter line among others is text" 'a\n\\:x\n' "     1|a${nl}     2|\\:x"
n "no newline at the end" 'a\nb' "     1|a${nl}     2|b"
t "a file" "     1|x" 0 sh -c 'echo x > f; "$1" f | tr "\t" "|"' sh "$U"
t "two files" "" 1 "$U" f f
t "a missing file" "" 1 "$U" nofile
t "a bad type" "" 1 "$U" -b x f
t "a bad format" "" 1 "$U" -n xx f
t "a bad width" "" 1 "$U" -w 0 f
t "a bad expression" "" 1 "$U" -b 'p\(' f
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: nl f -p: two operands" "" 1 "$U" f -p

done_tests nl
