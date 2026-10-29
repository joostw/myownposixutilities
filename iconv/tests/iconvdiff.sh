#!/bin/sh
# iconvdiff.sh - compare iconv with the iconv of the C library (glibc):
# conversions of valid text between many codesets, and conversions with
# the charmaps of /usr/share/i18n/charmaps (gzip files), joined with the
# UTF-8 charmap, against the conversions by codeset name.
# Run: sh tests/iconvdiff.sh ./iconv   (needs the system iconv, and gzip
# for the charmaps)
I=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
command -v iconv >/dev/null || { echo "iconvdiff: the system iconv is needed"; exit 2; }
T=${TMPDIR:-/tmp}/iconvdiff.$$
mkdir "$T" || exit 2
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
pass=0 fail=0
# text in UTF-8: ASCII, Latin, Greek, Cyrillic, symbols, CJK
printf 'Plain ASCII text, 0123456789 !"#$%%&()*+,-./:;<=>?@[\\]^_`{|}~\n' > ascii
{ cat ascii; printf 'Caf\303\251 na\303\257ve \303\205ngstr\303\266m \303\237 \303\274\303\266\303\244\n'; } > latin1
{ cat latin1; printf '\342\202\254 \305\223 \305\240 \305\276\n'; } > latin9
{ cat ascii; printf '\316\261\316\262\316\263 \316\224\316\251\n'; } > greek
{ cat ascii; printf '\320\277\321\200\320\270\320\262\320\265\321\202 \320\234\320\270\321\200\n'; } > cyr
{ cat ascii; printf '\346\227\245\346\234\254\350\252\236 \343\201\202\343\201\204\343\201\206\n'; } > jp
{ cat ascii; printf '\344\270\255\346\226\207 \351\237\223\352\265\255\354\226\264\n'; } > cjk
{ cat latin9 greek cyr jp cjk; printf '\360\237\230\200 \360\220\220\267\n'; } > all

# c TEXT CODESET: UTF-8 to CODESET and back, both iconvs the same
c() {
    "$I" -f UTF-8 -t "$2" "$1" > o1 2>/dev/null; s1=$?
    iconv -f UTF-8 -t "$2" "$1" > o2 2>/dev/null; s2=$?
    if [ $s1 = $s2 ] && cmp -s o1 o2; then pass=$((pass+1))
    else fail=$((fail+1)); echo "FAIL UTF-8 to $2 ($1): status $s1 and $s2"; fi
    if [ $s2 = 0 ]; then
        "$I" -f "$2" -t UTF-8 o2 > r1 2>/dev/null; s1=$?
        iconv -f "$2" -t UTF-8 o2 > r2 2>/dev/null; s2=$?
        if [ $s1 = $s2 ] && cmp -s r1 r2; then pass=$((pass+1))
        else fail=$((fail+1)); echo "FAIL $2 to UTF-8 ($1)"; fi
    fi
}
for cs in ASCII ISO-8859-1 CP1252 CP850 MACINTOSH; do c latin1 $cs; done
for cs in ISO-8859-15 CP1252; do c latin9 $cs; done
for cs in ISO-8859-7 CP1253; do c greek $cs; done
for cs in ISO-8859-5 KOI8-R KOI8-U CP1251 CP866; do c cyr $cs; done
for cs in EUC-JP SHIFT_JIS ISO-2022-JP CP932; do c jp $cs; done
for cs in GB18030 GBK BIG5 EUC-KR UHC; do c cjk $cs; done
for cs in UTF-16 UTF-16LE UTF-16BE UTF-32 UTF-32LE UTF-32BE UTF-7 UCS-2BE GB18030; do c all $cs; done
c latin1 IBM037
c ascii EBCDIC-US

# charmaps: CODESET's charmap joined with the UTF-8 charmap
CM=/usr/share/i18n/charmaps
if [ -d $CM ] && command -v gzip >/dev/null; then
    gzip -dc $CM/UTF-8.gz > UTF-8.cm
    for f in ISO-8859-1:latin1 ISO-8859-15:latin9 ISO-8859-7:greek KOI8-R:cyr CP1251:cyr IBM037:latin1 EUC-JP:jp SHIFT_JIS:jp EUC-KR:cjk BIG5:cjk; do
        cs=${f%:*} txt=${f#*:}
        [ -f $CM/$cs.gz ] || continue
        gzip -dc $CM/$cs.gz > $cs.cm
        iconv -f UTF-8 -t $cs $txt > in 2>/dev/null || continue
        "$I" -f ./$cs.cm -t ./UTF-8.cm in > o1 2>/dev/null; s1=$?
        iconv -f $cs -t UTF-8 in > o2
        if [ $s1 = 0 ] && cmp -s o1 o2; then pass=$((pass+1))
        else fail=$((fail+1)); echo "FAIL charmaps $cs to UTF-8 (status $s1)"; fi
        "$I" -f ./UTF-8.cm -t ./$cs.cm o2 > o3 2>/dev/null; s1=$?
        if [ $s1 = 0 ] && cmp -s o3 in; then pass=$((pass+1))
        else fail=$((fail+1)); echo "FAIL charmaps UTF-8 to $cs (status $s1)"; fi
    done
fi
echo "iconvdiff: $pass passed, $fail failed"
[ "$fail" = 0 ]
