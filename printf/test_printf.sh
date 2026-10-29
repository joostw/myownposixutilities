#!/bin/sh
# test_printf.sh - tests for printf (POSIX.1-2024).  Run: sh test_printf.sh
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
done_tests() { echo "$1: $pass passed, $fail failed"; [ "$fail" = 0 ]; }
U=${U:-$PWD/printf}
# a UTF-8 locale for the cases with characters outside ASCII (POSIX
# names none): $UTF8_LOCALE, or C.UTF-8, or any one that locale -a lists
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
u8() { [ -n "$U8" ]; }

# plain text and escapes
tb "text" "a b c" "$U" "a b c"
tb "newline" "a \n" "$U" 'a\n'
tb "escapes" "\\ \a \b \f \n \r \t \v" "$U" '\\\a\b\f\n\r\t\v'
tb "octal" "A B 001 \0 \n" "$U" '\101\102\1\0\n'
tb "octal: three digits at most" "001 2" "$U" '\0012'
tb "quotes" "\" '" "$U" '\"'"\\'"
tb "a backslash at the end" "a \\" "$U" 'a\'
tb "another backslash sequence" "\\ q" "$U" '\q'
tb "%%" "%" "$U" '%%'
tb "--" "x" "$U" -- x
t "no format" "" 2 "$U"

# integers
t "%d" "42" 0 "$U" '%d' 42
t "%i" "-7" 0 "$U" '%i' -7
t "+ sign" "12" 0 "$U" '%d' +12
t "hexadecimal" "31" 0 "$U" '%d' 0x1f
t "octal argument" "8" 0 "$U" '%d' 010
t "width" "   42|42   " 0 "$U" '%5d|%-5d' 42 42
t "flags" "00007 +7  7" 0 "$U" '%05d %+d % d' 7 7 7
t "precision" "007" 0 "$U" '%.3d' 7
t "%x %X %o" "ff FF 10" 0 "$U" '%x %X %o' 255 255 8
t "# flag" "0xff 010" 0 "$U" '%#x %#o' 255 8
t "%u of -1" "18446744073709551615" 0 "$U" '%u' -1
t "%x of -1" "ffffffffffffffff" 0 "$U" '%x' -1
t "a character constant" "65 66" 0 "$U" '%d %d' "'A" '"B'
t "a character constant alone" "0" 0 "$U" '%d' "'"
u8 && t "a character constant outside ASCII" "233" 0 env LC_ALL=$U8 "$U" '%d' "'é"
t "missing: zero" "0" 0 "$U" '%d'
t "empty: zero" "0" 0 "$U" '%d' ''
t "not a number" "0" 1 "$U" '%d' abc
t "not completely converted: the value so far" "12" 1 "$U" '%d' 12abc
t "out of range" "9223372036854775807" 1 "$U" '%d' 99999999999999999999
t "out of range, negative" "-9223372036854775808" 1 "$U" '%d' -99999999999999999999
t "out of range for %u" "18446744073709551615" 1 "$U" '%u' 99999999999999999999
t "leading blanks" "5" 0 "$U" '%d' ' 5'
t "an error does not stop the output" "0 3" 1 "$U" '%d %d' x 3
t "* width" "   42" 0 "$U" '%*d' 5 42
t "* negative width" "42   |" 0 "$U" '%*d|' -5 42
t "* precision" "ab" 0 "$U" '%.*s' 2 abcdef

# floating point
t "%f %e %g" "1.500000 1.500000e+00 1.5" 0 "$U" '%f %e %g' 1.5 1.5 1.5
t "%.3f %10.2e" "3.142   3.14e+04" 0 "$U" '%.3f %10.2e' 3.14159 31415.9
t "%E %G %F" "1.000000E+00 1E+10 2.000000" 0 "$U" '%E %G %F' 1 1e10 2
t "%a" "0x8p-3" 0 "$U" '%a' 1
t "floating: not completely converted" "1.000000" 1 "$U" '%f' 1x
t "floating: missing" "0.000000" 0 "$U" '%f'

# strings and characters
t "%s" "hello" 0 "$U" '%s' hello
t "%s width, precision" " ab|ab |  a" 0 "$U" '%3s|%-3s|%3.1s' ab ab ab
t "%5.2s" "   ab" 0 "$U" '%5.2s' abcdef
t "%c: the first byte" "ad" 0 "$U" '%c%c' abc d
tb "%c of an empty string: nothing" "x" "$U" '%cx' ''
t "%s missing: empty" "[]" 0 "$U" '[%s]'
tb "%b escapes" "a \t b \n c" "$U" '%b' 'a\tb\nc'
tb "%b octal with 0" "A A" "$U" '%b' '\0101\101'
tb "%b \\0 alone" "\0 x" "$U" '%b' '\0x'
tb "%b four digits after 0" "A 2" "$U" '%b' '\01012'
tb "%b \\c stops everything" "x" "$U" '%b%s\n' 'x\cy' z
tb "%b other sequences" "\\ q \\" "$U" '%b' '\q\\'
t "%b width and precision" "  ab|a |" 0 "$U" '%4.2b|%-2.1b|' 'abc' 'abc'
tb "%b with NUL bytes" "a \0 b" "$U" '%b' 'a\0b'
tb "%s does not expand escapes" "a \\ n" "$U" '%s' 'a\n'

# reuse of the format
t "reuse" "a b
c d" 0 "$U" '%s %s\n' a b c d
t "reuse: missing at the end" "a b
c " 0 "$U" '%s %s\n' a b c
t "no conversions, arguments ignored" "x" 0 "$U" 'x' a b
t "numbered" "b a" 0 "$U" '%2$s %1$s' a b
t "numbered, reused" "b a
d c" 0 "$U" '%2$s %1$s\n' a b c d
t "numbered twice" "a a b" 0 "$U" '%1$s %1$s %2$s' a b
t "numbered: a missing argument" "a " 1 "$U" '%1$s %3$s' a b
t "numbered with %%" "100%" 0 "$U" '%1$d%%' 100
t "numbered * width" "   x" 0 "$U" '%1$*2$s' x 4

# errors
t "an invalid conversion" "a" 1 "$U" 'a%y%s' b
t "an unfinished conversion" "a" 1 "$U" 'a%'
[ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" x >/dev/full' sh "$U"

done_tests printf
