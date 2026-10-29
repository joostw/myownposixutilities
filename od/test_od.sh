#!/bin/sh
# test_od.sh - tests for od (POSIX.1-2024).  Run: sh test_od.sh
# The expected output is that of GNU od 9.4 for the same input, except for
# the exit status of usage errors (2 here) and the decimal offset operand
# "10." (which GNU od does not take).
cd "$(dirname "$0")" || exit 2
pass=0 fail=0
# t NAME EXPECTED-STDOUT EXPECTED-STATUS COMMAND...  (stdout compared without trailing newlines)
t() {
    _n=$1 _e=$2 _s=$3; shift 3
    _o=$("$@" 2>/dev/null); _r=$?
    if [ "$_o" = "$_e" ] && [ "$_r" = "$_s" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s] status %s\n  want: [%s] status %s\n' "$_n" "$_o" "$_r" "$_e" "$_s"; fi
}
done_tests() { echo "$1: $pass passed, $fail failed"; [ "$fail" = 0 ]; }
U=${U:-$PWD/od}
# a UTF-8 locale for the cases with characters outside ASCII (POSIX
# names none): $UTF8_LOCALE, or C.UTF-8, or any one that locale -a lists
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
u8() { [ -n "$U8" ]; }
T=${TMPDIR:-/tmp}/test_od.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
# the byte order is implementation-defined: the cases with items of more
# than one byte expect the least significant byte first, and are skipped
# (not counted) otherwise
le() { [ "$(printf '\001\000' | "$U" -A n -t x2)" = " 0001" ]; }

printf 'Hello, world!\n\0\377\200abc' > a
awk 'BEGIN { for (i = 0; i < 100; i++) printf "x" }' > rep
printf '\001\000\002\000\377\377\377\177' > w
printf '\0\0\200\077\0\0\0\0\0\0\360\077' > fl
: > e
printf 'abcdefghij' > b10
le && t "the default: -t o2" "0000000 062510 066154 026157 073440 071157 062154 005041 177400
0000020 060600 061542
0000024" 0 sh -c '"$1" a' sh "$U"
t "-A d -t x1" "0000000 48 65 6c 6c 6f 2c 20 77 6f 72 6c 64 21 0a 00 ff
0000016 80 61 62 63
0000020" 0 sh -c '"$1" -A d -t x1 a' sh "$U"
le && t "-A x -t d2" "000000  25928  27756  11375  30496  29295  25708   2593   -256
000010  24960  25442
000014" 0 sh -c '"$1" -A x -t d2 a' sh "$U"
le && t "-A n -t u4" " 1819043144 1998597231 1684828783 4278192673
 1667391872" 0 sh -c '"$1" -A n -t u4 a' sh "$U"
t "-A o -t o1" "0000000 110 145 154 154 157 054 040 167 157 162 154 144 041 012 000 377
0000020 200 141 142 143
0000024" 0 sh -c '"$1" -A o -t o1 a' sh "$U"
le && t "-t o1 -t x2: aligned" "0000000 110 145 154 154 157 054 040 167 157 162 154 144 041 012 000 377
           6548    6c6c    2c6f    7720    726f    646c    0a21    ff00
0000020 200 141 142 143
           6180    6362
0000024" 0 sh -c '"$1" -t o1 -t x2 a' sh "$U"
le && t "-t x1 -t o2: aligned" "0000000  48 65  6c 6c  6f 2c  20 77  6f 72  6c 64  21 0a  00 ff
        062510 066154 026157 073440 071157 062154 005041 177400
0000020  80 61  62 63
        060600 061542
0000024" 0 sh -c '"$1" -t x1 -t o2 a' sh "$U"
t "-t a" "0000000   H   e   l   l   o   ,  sp   w   o   r   l   d   !  nl nul del
0000020 nul   a   b   c
0000024" 0 sh -c '"$1" -t a a' sh "$U"
t "-t c" "0000000   H   e   l   l   o   ,       w   o   r   l   d   !  \\n  \\0 377
0000020 200   a   b   c
0000024" 0 sh -c '"$1" -t c a' sh "$U"
t "-c" "0000000   H   e   l   l   o   ,       w   o   r   l   d   !  \\n  \\0 377
0000020 200   a   b   c
0000024" 0 sh -c '"$1" -c a' sh "$U"
t "-t a -t c" "0000000   H   e   l   l   o   ,  sp   w   o   r   l   d   !  nl nul del
          H   e   l   l   o   ,       w   o   r   l   d   !  \\n  \\0 377
0000020 nul   a   b   c
        200   a   b   c
0000024" 0 sh -c '"$1" -t a -t c a' sh "$U"
le && t "-t d1u4: two types in one string" "0000000   72  101  108  108  111   44   32  119  111  114  108  100   33   10    0   -1
                 1819043144          1998597231          1684828783          4278192673
0000020 -128   97   98   99
                 1667391872
0000024" 0 sh -c '"$1" -t d1u4 a' sh "$U"
le && t "-t xC -t dS -t uI" "0000000  48 65  6c 6c  6f 2c  20 77  6f 72  6c 64  21 0a  00 ff
         25928  27756  11375  30496  29295  25708   2593   -256
           1819043144    1998597231    1684828783    4278192673
0000020  80 61  62 63
         24960  25442
           1667391872
0000024" 0 sh -c '"$1" -t xC -t dS -t uI a' sh "$U"
le && t "-t d8 -t x8" "0000000  8583909746840200520   -72046455502900625
            77202c6f6c6c6548     ff000a21646c726f
0000020           1667391872
            0000000063626180
0000024" 0 sh -c '"$1" -t d8 -t x8 a' sh "$U"
le && t "-t o8 -t u8" "0000000 0734401306755433062510 1774000242054433071157
           8583909746840200520   18374697618206650991
0000020 0000000000014330460600
                    1667391872
0000024" 0 sh -c '"$1" -t o8 -t u8 a' sh "$U"
le && t "-t d2 signed" "0000000      1      2     -1  32767
0000010" 0 sh -c '"$1" -t d2 w' sh "$U"
le && t "-t u2 -t d4 -t x4" "0000000     1     2 65535 32767
             131073  2147483647
           00020001    7fffffff
0000010" 0 sh -c '"$1" -t u2 -t d4 -t x4 w' sh "$U"
le && t "-t f4" "0000000               1               0           1.875
0000014" 0 sh -c '"$1" -t f4 fl' sh "$U"
le && t "-t f8" "0000000         5.263544247e-315         5.299808824e-315
0000014" 0 sh -c '"$1" -t f8 fl' sh "$U"
le && t "-t fF -t fD" "0000000               1               0           1.875
                       5.263544247e-315                5.299808824e-315
0000014" 0 sh -c '"$1" -t fF -t fD fl' sh "$U"
le && t "-t f4 -t x1: aligned" "0000000               1               0           1.875
         00  00  80  3f  00  00  00  00  00  00  f0  3f
0000014" 0 sh -c '"$1" -t f4 -t x1 fl' sh "$U"
t "-b" "0000000 110 145 154 154 157 054 040 167 157 162 154 144 041 012 000 377
0000020 200 141 142 143
0000024" 0 sh -c '"$1" -b a' sh "$U"
le && t "-d" "0000000 25928 27756 11375 30496 29295 25708  2593 65280
0000020 24960 25442
0000024" 0 sh -c '"$1" -d a' sh "$U"
le && t "-o" "0000000 062510 066154 026157 073440 071157 062154 005041 177400
0000020 060600 061542
0000024" 0 sh -c '"$1" -o a' sh "$U"
le && t "-s" "0000000      1      2     -1  32767
0000010" 0 sh -c '"$1" -s w' sh "$U"
le && t "-x" "0000000 6548 6c6c 2c6f 7720 726f 646c 0a21 ff00
0000020 6180 6362
0000024" 0 sh -c '"$1" -x a' sh "$U"
le && t "-bcx: in order" "0000000 110 145 154 154 157 054 040 167 157 162 154 144 041 012 000 377
          H   e   l   l   o   ,       w   o   r   l   d   !  \\n  \\0 377
           6548    6c6c    2c6f    7720    726f    646c    0a21    ff00
0000020 200 141 142 143
        200   a   b   c
           6180    6362
0000024" 0 sh -c '"$1" -bcx a' sh "$U"
t "-c -t x1: in order" "0000000   a   b   c   d   e   f   g   h   i   j
         61  62  63  64  65  66  67  68  69  6a
0000012" 0 sh -c '"$1" -c -t x1 b10' sh "$U"
t "repeated blocks: *" "0000000 78 78 78 78 78 78 78 78 78 78 78 78 78 78 78 78
*
0000140 78 78 78 78
0000144" 0 sh -c '"$1" -t x1 rep' sh "$U"
le && t "-v: no *" "0000000 78 78 78 78 78 78 78 78 78 78 78 78 78 78 78 78
0000020 78 78 78 78 78 78 78 78 78 78 78 78 78 78 78 78
0000040 78 78 78 78 78 78 78 78 78 78 78 78 78 78 78 78
0000060 78 78 78 78 78 78 78 78 78 78 78 78 78 78 78 78
0000100 78 78 78 78 78 78 78 78 78 78 78 78 78 78 78 78
0000120 78 78 78 78 78 78 78 78 78 78 78 78 78 78 78 78
0000140 78 78 78 78
0000144" 0 sh -c '"$1" -v -t x1 rep' sh "$U"
t "-j" "0000003 6c 6f 2c 20 77 6f 72 6c 64 21 0a 00 ff 80 61 62
0000023 63
0000024" 0 sh -c '"$1" -t x1 -j 3 a' sh "$U"
t "-j hexadecimal" "0000020 80 61 62 63
0000024" 0 sh -c '"$1" -t x1 -j 0x10 a' sh "$U"
t "-j octal" "0000010 6f 72 6c 64 21 0a 00 ff 80 61 62 63
0000024" 0 sh -c '"$1" -t x1 -j 010 a' sh "$U"
t "-j to the end" "0000024" 0 sh -c '"$1" -j 20 a' sh "$U"
t "-N" "0000000 48 65 6c 6c 6f
0000005" 0 sh -c '"$1" -t x1 -N 5 a' sh "$U"
t "-N hexadecimal" "0000000 48 65 6c 6c 6f 2c 20 77 6f 72 6c 64 21 0a 00 ff
0000020 80
0000021" 0 sh -c '"$1" -t x1 -N 0x11 a' sh "$U"
t "-N more than there is" "0000000   a   b   c   d   e   f   g   h   i   j
0000012" 0 sh -c '"$1" -N 100 -c b10' sh "$U"
t "-N 0" "0000000" 0 sh -c '"$1" -N 0 a' sh "$U"
t "-j -N" "0000003 6c 6f 2c 20
0000007" 0 sh -c '"$1" -t x1 -j 3 -N 4 a' sh "$U"
t "-j across files" "0000014   c   d   e   f   g   h   i   j
0000024" 0 sh -c '"$1" -c -j 12 b10 b10' sh "$U"
t "files concatenated" "0000000   a   b   c   d   e   f   g   h   i   j   a   b   c   d   e   f
0000020   g   h   i   j
0000024" 0 sh -c '"$1" -c b10 b10' sh "$U"
le && t "an empty file" "0000000" 0 sh -c '"$1" e' sh "$U"
t "an empty file and another" "0000000   a   b   c   d   e   f   g   h   i   j
0000012" 0 sh -c '"$1" -c e b10' sh "$U"
le && t "an odd byte: padded with zero" "0000000 6261 0063
0000003" 0 sh -c '"$1" -t x2 -N 3 b10' sh "$U"
t "-j 1b: blocks" "" 1 sh -c '"$1" -j 1b -N 2 -c rep' sh "$U"
t "-j 1k" "" 1 sh -c '"$1" -j 1k e a' sh "$U"
t "the offset operand +" "0000004   e   f   g   h   i   j
0000012" 0 sh -c '"$1" -c b10 +4' sh "$U"
t "the offset operand, two operands" "0000004   e   f   g   h   i   j
0000012" 0 sh -c '"$1" -c b10 4' sh "$U"
t "the offset operand in decimal" "0000012" 0 sh -c '"$1" -c b10 10.' sh "$U"
t "the offset operand, octal" "0000010   i   j
0000012" 0 sh -c '"$1" -c b10 +10' sh "$U"
t "the offset operand only" "0000003   d   e   f   g   h   i   j
0000012" 0 sh -c '"$1" -c +3 <b10' sh "$U"
t "standard input" "0000000   a   b   c   d   e   f   g   h   i   j
0000012" 0 sh -c '"$1" -c <b10' sh "$U"
t "- is standard input" "0000000   a   b   c   d   e   f   g   h   i   j
0000012" 0 sh -c '"$1" -c - <b10' sh "$U"
t "a missing file" "0000000   a   b   c   d   e   f   g   h   i   j
0000012" 1 sh -c '"$1" -c nofile b10' sh "$U"
t "-j beyond the input" "" 1 sh -c '"$1" -j 100 b10' sh "$U"
t "a bad type" "" 2 sh -c '"$1" -t q a' sh "$U"
t "a bad size" "" 2 sh -c '"$1" -t d3 a' sh "$U"
t "a bad float size" "" 2 sh -c '"$1" -t f2 a' sh "$U"
t "a bad base" "" 2 sh -c '"$1" -A q a' sh "$U"
t "a bad -j" "" 2 sh -c '"$1" -j x a' sh "$U"

# -c with multibyte characters: the character in its first byte's area,
# ** for the others; a cut character in octal
printf 'h\303\251llo \342\202\254 x\303' > mb
u8 && t "-c UTF-8" "0000000   h   é  **   l   l   o       €  **  **       x 303
0000015" 0 env LC_ALL=$U8 "$U" -c mb
u8 && t "-c UTF-8, -N cuts a character" "0000000   h 303
0000002" 0 env LC_ALL=$U8 "$U" -c -N 2 mb
t "-c in the C locale: octal" "0000000   h 303 251   l   l   o     342 202 254       x 303
0000015" 0 "$U" -c mb
[ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" a >/dev/full' sh "$U"

# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: od -c f -v: -v is a file" "0000000   x  \\n
0000002" 1 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" -c f -v; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests od
