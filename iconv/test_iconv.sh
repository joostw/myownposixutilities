#!/bin/sh
# test_iconv.sh - tests for iconv (POSIX.1-2024).  Run: sh test_iconv.sh
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
U=${U:-$PWD/iconv}
T=${TMPDIR:-/tmp}/test_iconv.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# x COMMAND...: its output in octal, one line; its status
x() {
    { "$@"; echo $? > "$T/x.st"; } | od -An -b | tr -s ' \n' '  ' | sed 's/^ //; s/ $//'
    return "$(cat "$T/x.st")"
}

# conversions with iconv_open() names
t "UTF-8 to ISO-8859-1" "150 351 154 154 157 012" 0 x sh -c 'printf "h\303\251llo\n" | "$1" -f UTF-8 -t ISO-8859-1' sh "$U"
t "ISO-8859-1 to UTF-8" "150 303 251 154 154 157 012" 0 x sh -c 'printf "h\351llo\n" | "$1" -f ISO-8859-1 -t UTF-8' sh "$U"
t "UTF-8 to ISO-8859-15: the euro" "244" 0 x sh -c 'printf "\342\202\254" | "$1" -f UTF-8 -t ISO-8859-15' sh "$U"
t "UTF-8 to UTF-16BE" "000 101 000 142" 0 x sh -c 'printf Ab | "$1" -f UTF-8 -t UTF-16BE' sh "$U"
t "UTF-16LE to UTF-8" "101 142" 0 x sh -c 'printf "A\000b\000" | "$1" -f UTF-16LE -t UTF-8' sh "$U"
t "UTF-8 to UTF-32BE: beyond the BMP" "000 001 366 000" 0 x sh -c 'printf "\360\237\230\200" | "$1" -f UTF-8 -t UTF-32BE' sh "$U"
t "a stateful codeset: to ISO-2022-JP and back" "343 201 202 012" 0 x sh -c 'printf "\343\201\202\n" | "$1" -f UTF-8 -t ISO-2022-JP | "$1" -f ISO-2022-JP -t UTF-8' sh "$U"
t "ISO-2022-JP: back to ASCII at the end" "033 044 102 044 042 033 050 102" 0 x sh -c 'printf "\343\201\202" | "$1" -f UTF-8 -t ISO-2022-JP' sh "$U"
t "the same codeset both ways" "hello" 0 sh -c 'echo hello | "$1" -f UTF-8 -t UTF-8' sh "$U"
ok "a big file through UTF-16 and back" sh -c 'awk "BEGIN { for (i = 0; i < 30000; i++) printf \"line %d \\303\\251\\342\\202\\254\\n\", i }" > big; "$1" -f UTF-8 -t UTF-16 big | "$1" -f UTF-16 -t UTF-8 | cmp -s - big' sh "$U"
ok "a character across a read boundary" sh -c 'awk "BEGIN { for (i = 0; i < 70000; i++) printf \"\\303\\251\" }" > acc; "$1" -f UTF-8 -t ISO-8859-1 acc | "$1" -f ISO-8859-1 -t UTF-8 | cmp -s - acc' sh "$U"
t "several files, and -" "a${nl}b${nl}c" 0 sh -c 'echo a > fa; echo c > fc; echo b | "$1" -f UTF-8 -t ISO-8859-1 fa - fc' sh "$U"
t "no file: the standard input" "a" 0 sh -c 'echo a | "$1" -f UTF-8 -t ISO-8859-1' sh "$U"

# the locale's codeset when -f or -t is missing
t "no -t: the codeset of the locale (C: ASCII)" "hi" 0 sh -c 'echo hi | "$1" -f ISO-8859-1' sh "$U"
t "no -f: the codeset of the locale" "hi" 0 sh -c 'echo hi | "$1" -t UTF-16LE | "$1" -f UTF-16LE' sh "$U"
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
if [ -n "$U8" ]; then
    t "no -t in a UTF-8 locale" "303 251" 0 x sh -c 'printf "\351" | LC_ALL=$2 "$1" -f ISO-8859-1' sh "$U" "$U8"
fi

# bad characters
t "invalid input: stops there, status 1" "141" 1 x sh -c 'printf "a\377b" | "$1" -f UTF-8 -t ISO-8859-1' sh "$U"
t "invalid input, -c: left out, status 1" "141 142" 1 x sh -c 'printf "a\377b" | "$1" -c -f UTF-8 -t ISO-8859-1' sh "$U"
t "not in the output codeset: stops there" "141" 1 x sh -c 'printf "a\342\202\254b" | "$1" -f UTF-8 -t ISO-8859-1' sh "$U"
t "not in the output codeset, -c: left out" "141 142" 1 x sh -c 'printf "a\342\202\254b" | "$1" -c -f UTF-8 -t ISO-8859-1' sh "$U"
t "-c leaves out a whole character of UTF-16" "101 102" 1 x sh -c 'printf "A\000\254\040B\000" | "$1" -c -f UTF-16LE -t ISO-8859-1' sh "$U"
t "the message names the file" "yes" 0 sh -c 'printf "a\377" > badf; "$1" -f UTF-8 -t ISO-8859-1 badf 2>&1 >/dev/null | grep -q "badf" && echo yes' sh "$U"
t "the message for a missing character names it" "yes" 0 sh -c 'printf "\342\202\254" | "$1" -f UTF-8 -t ISO-8859-1 2>&1 >/dev/null | grep -q "U+20AC" && echo yes' sh "$U"
t "-s: no message" "" 1 sh -c 'printf "a\377b" | "$1" -s -f UTF-8 -t ISO-8859-1 2>&1 >/dev/null' sh "$U"
t "-cs: no message, status 1" "" 1 sh -c 'printf "a\377b" | "$1" -cs -f UTF-8 -t ISO-8859-1 2>&1 >/dev/null' sh "$U"
t "an incomplete character at the end" "141" 1 x sh -c 'printf "a\303" | "$1" -f UTF-8 -t ISO-8859-1' sh "$U"
t "after a bad file the next is converted" "a${nl}b" 1 sh -c 'printf "a\n\377\n" > b1; echo b > b2; "$1" -f UTF-8 -t ISO-8859-1 b1 b2' sh "$U"

# charmaps (XBD 6.4)
cat > one.cm <<'CM'
<code_set_name> ONE
<comment_char> %
<escape_char> /
% a comment
<mb_cur_max> 1
CHARMAP
<NUL>       /x00
<newline>   /x0a      a comment after the encoding
<space>     /d32
<A>         /101
<B>         /x42
<C>         /x43
<e-acute>   /xe9
<d0>...<d9> /x30
<lt>        /x3c
<x/>y>      /x3e
END CHARMAP
WIDTH
<A>...<C> 1
END WIDTH
CM
cat > two.cm <<'CM'
<code_set_name> TWO
<mb_cur_max> 2
CHARMAP
<NUL>       \x00
<newline>   \x0a
<space>     \x20
<A>         \xc1
<B>         \xc2
<e-acute>   \x8e\x51
<d0>..<d9>  \xf0
<lt>        \x4c
<x\>y>      \x6e
END CHARMAP
CM
t "two charmaps: joined on the names" "301 302 040 216 121 012" 0 x sh -c 'printf "AB \351\n" | "$1" -f ./one.cm -t ./two.cm' sh "$U"
t "back again: two-byte characters" "101 102 040 351 012" 0 x sh -c 'printf "\301\302 \216\121\n" | "$1" -f ./two.cm -t ./one.cm' sh "$U"
t "ranges: decimal ... and hexadecimal .." "360 361 362 371" 0 x sh -c 'printf 0129 | "$1" -f ./one.cm -t ./two.cm' sh "$U"
t "the escape character in a name" "156 114" 0 x sh -c 'printf "><" | "$1" -f ./one.cm -t ./two.cm' sh "$U"
t "a name not in the other charmap: stops" "301" 1 x sh -c 'printf AC | "$1" -f ./one.cm -t ./two.cm' sh "$U"
t "a name not in the other charmap, -c" "301 302" 1 x sh -c 'printf ACB | "$1" -c -f ./one.cm -t ./two.cm' sh "$U"
t "bytes not in the charmap" "301" 1 x sh -c 'printf "A\001" | "$1" -f ./one.cm -t ./two.cm' sh "$U"
t "an incomplete two-byte character at the end" "101" 1 x sh -c 'printf "\301\216" | "$1" -f ./two.cm -t ./one.cm' sh "$U"
t "a two-byte character across a read boundary" "yes" 0 sh -c 'awk "BEGIN { for (i = 0; i < 40000; i++) printf \"\\216\\121\" }" > tb; "$1" -f ./two.cm -t ./one.cm tb | "$1" -f ./one.cm -t ./two.cm | cmp -s - tb && echo yes' sh "$U"
t "a charmap with a codeset name: an error" "" 1 sh -c 'echo A | "$1" -f ./one.cm -t UTF-8' sh "$U"
t "a missing charmap" "" 1 sh -c 'echo A | "$1" -f ./none.cm -t ./two.cm' sh "$U"
t "not a charmap" "" 1 sh -c 'echo A | "$1" -f ./test_iconv_x -t ./two.cm 2>/dev/null; echo junk > nc; echo A | "$1" -f ./nc -t ./two.cm' sh "$U"

# -l
t "-l: the names, one a line" "yes" 0 sh -c '"$1" -l > l; grep -q "^UTF-8\$" l && grep -q "^ISO-8859-1\$" l && echo yes' sh "$U"
ok "-l: every name can be used" sh -c '"$1" -l | while read n; do "$1" -f UTF-8 -t "$n" < /dev/null > /dev/null 2>&1 || exit 1; done' sh "$U"
t "-l with an operand" "" 2 "$U" -l file

# errors
t "an unknown codeset" "" 1 sh -c 'echo a | "$1" -f NO-SUCH-CODESET -t UTF-8' sh "$U"
t "a missing file: the others converted" "a" 1 sh -c 'echo a > ok1; "$1" -f UTF-8 -t ISO-8859-1 nonexistent ok1' sh "$U"
t "an unknown option" "" 2 "$U" -x
t "-f without its argument" "" 2 "$U" -f
# getopt() must not take an option after an operand for one (no permuting)
t "an option after an operand is an operand" "a" 1 sh -c 'echo a > op; "$1" -f UTF-8 -t ISO-8859-1 op -c' sh "$U"

done_tests iconv
