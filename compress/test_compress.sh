#!/bin/sh
# test_compress.sh - tests for compress (POSIX.1-2024).  Run: sh test_compress.sh
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
U=${U:-$PWD/compress}
T=${TMPDIR:-/tmp}/test_compress.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
# ptyrun FDS COMMAND...: COMMAND with the descriptors FDS on a terminal (../testlib/ptyrun.py)
TL=$(cd ../testlib && pwd) || exit 2; export TL
ptyrun() { python3 "$TL/ptyrun.py" "$@"; }
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
Z=${U%/*}/uncompress C=${U%/*}/zcat
# b FILE: the first N bytes of FILE in octal, one line
b() { od -An -b "$1" | tr -s ' \n' '  ' | cut -d ' ' -f "2-$(($2 + 1))"; }
# mode and time of a file, as ls -l shows them
mt() { ls -l "$1" | awk '{ print $1, $6, $7, $8 }'; }

printf 'hello\n' > hello
awk 'BEGIN { for (i = 0; i < 3000; i++) print "line", i, "of some text that repeats", i % 7 }' > text
awk 'BEGIN { srand(11); for (i = 0; i < 40000; i++) printf "%c", int(rand() * 256) }' > rnd 2>/dev/null
awk 'BEGIN { for (i = 0; i < 256; i++) printf "%c", i }' > all 2>/dev/null
for i in 1 2 3 4 5 6 7 8; do cat all; done > all8

# the formats
t "lzw: the header, 16 bits in block mode" "037 235 220" 0 sh -c '"$1" -c text | od -An -b | sed 1q | cut -c 2-12' sh "$U"
t "lzw: -b 12" "037 235 214" 0 sh -c '"$1" -c -b 12 text | od -An -b | sed 1q | cut -c 2-12' sh "$U"
t "lzw: -b 9" "037 235 211" 0 sh -c '"$1" -c -b 9 text | od -An -b | sed 1q | cut -c 2-12' sh "$U"
t "lzw: the known codes" "$(printf '\037\235\220\141\304\004\034\050\260\040\101\005' | od -An -b)" 0 sh -c 'printf "abababababababab\n" | "$1" | od -An -b' sh "$U"
t "gzip (-g): the header" "037 213 010 000" 0 sh -c '"$1" -c -g text | od -An -b | sed 1q | cut -c 2-16' sh "$U"
t "-m gzip" "037 213 010" 0 sh -c '"$1" -c -m gzip text | od -An -b | sed 1q | cut -c 2-12' sh "$U"
t "-m deflate" "037 213 010" 0 sh -c '"$1" -c -m deflate text | od -An -b | sed 1q | cut -c 2-12' sh "$U"
t "-m lzw" "037 235 220" 0 sh -c '"$1" -c -m lzw text | od -An -b | sed 1q | cut -c 2-12' sh "$U"
t "-g then -m lzw: the last counts" "037 235" 0 sh -c '"$1" -c -g -m lzw text | od -An -b | sed 1q | cut -c 2-8' sh "$U"
t "-m lzw then -g: the last counts" "037 213" 0 sh -c '"$1" -c -m lzw -g text | od -An -b | sed 1q | cut -c 2-8' sh "$U"
t "gzip: the time of the file in the header" "144 000 000 000" 0 sh -c 'cp hello h; TZ=UTC0 touch -t 197001010001.40 h; "$1" -c -g h | od -An -b | sed 1q | cut -c 18-32' sh "$U"
t "gzip: the operating system is Unix" "003" 0 sh -c '"$1" -c -g text | od -An -b | sed 1q | cut -c 38-40' sh "$U"
t "gzip: the trailer: CRC-32 and size" "040 060 072 066 006 000 000 000" 0 sh -c '"$1" -c -g hello | od -An -b | tr -s " \n" "  " | awk "{ for (i = NF - 7; i <= NF; i++) printf \"%s%s\", \$i, (i < NF ? \" \" : \"\") }"' sh "$U"

# round trips
for f in hello text rnd all8; do
    for o in "" "-b 9" "-b 12" "-g" "-g -b 1" "-g -b 9" "-m deflate -b 4"; do
        ok "round trip of $f: $o" sh -c '"$1" -c -f $3 "$2" | "$1" -d | cmp -s - "$2"' sh "$U" "$f" "$o"
    done
done
ok "round trip of an empty file" sh -c ': > e; "$1" -c -f e | "$1" -d | cmp -s - e' sh "$U"
ok "round trip of an empty file, gzip" sh -c ': > e; "$1" -c -f -g e | "$1" -d | cmp -s - e' sh "$U"
ok "round trip: 9 bits, the table full many times" sh -c 'for i in 1 2 3 4 5 6; do cat rnd text; done > big; "$1" -c -f -b 9 big | "$1" -d | cmp -s - big' sh "$U"
ok "round trip: 16 bits, a big file" sh -c 'for i in 1 2 3 4 5 6 7 8 9 10; do cat rnd text all8; done > big2; "$1" -c -f big2 | "$1" -d | cmp -s - big2' sh "$U"
ok "round trip: gzip, a big file" sh -c '"$1" -c -f -g big2 | "$1" -d | cmp -s - big2' sh "$U"
ok "round trip: a long run" sh -c 'awk "BEGIN { for (i = 0; i < 100000; i++) printf \"x\" }" > run; "$1" -c run | "$1" -d | cmp -s - run && "$1" -c -g run | "$1" -d | cmp -s - run' sh "$U"
ok "text gets smaller (lzw)" sh -c '[ "$("$1" -c text | wc -c)" -lt "$(($(wc -c < text) / 4))" ]' sh "$U"
ok "text gets smaller (gzip)" sh -c '[ "$("$1" -c -g text | wc -c)" -lt "$(($(wc -c < text) / 4))" ]' sh "$U"
ok "-b 9 for gzip packs more than -b 1" sh -c '[ "$("$1" -c -g -b 9 text | wc -c)" -le "$("$1" -c -g -b 1 text | wc -c)" ]' sh "$U"

# known input
t "expand: lzw" "abababababababab" 0 sh -c 'printf "\037\235\220\141\304\004\034\050\260\040\101\005" | "$1" -d' sh "$U"
t "expand: lzw, 9 bits" "abababababababab" 0 sh -c 'printf "\037\235\211\141\304\004\034\050\260\040\101\005" | "$1" -d' sh "$U"
t "expand: gzip" "hello" 0 sh -c 'printf "\037\213\010\000\000\000\000\000\000\003\313\110\315\311\311\347\002\000\040\060\072\066\006\000\000\000" | "$1" -d' sh "$U"
t "expand: gzip with every header field" "hello" 0 sh -c 'printf "\037\213\010\037\000\000\000\000\000\003\003\000\141\142\143\156\141\155\145\000\143\157\155\155\145\156\164\000\167\341\313\110\315\311\311\347\002\000\040\060\072\066\006\000\000\000" | "$1" -d' sh "$U"
t "expand: gzip, a stored block" "hello" 0 sh -c 'printf "\037\213\010\000\000\000\000\000\000\003\001\006\000\371\377\150\145\154\154\157\012\040\060\072\066\006\000\000\000" | "$1" -d' sh "$U"
t "expand: two gzip members" "hello${nl}line 0 of some text that repeats 0" 0 sh -c '{ "$1" -c -g hello; "$1" -c -g text; } | "$1" -d | sed 2q' sh "$U"
t "expand: the format is found from the data" "hello${nl}hello" 0 sh -c '"$1" -c -g hello > x.Z; "$1" -c hello > y.gz; "$1" -dc x.Z y.gz' sh "$U"

# bad input
t "not in compressed format" "" 1 sh -c 'echo plain | "$1" -d' sh "$U"
t "empty input" "" 1 sh -c ': | "$1" -d' sh "$U"
t "a truncated gzip stream" "" 1 sh -c '"$1" -c -g text | dd bs=100 count=1 2>/dev/null | "$1" -d >/dev/null' sh "$U"
t "a gzip CRC error" "" 1 sh -c 'printf "\037\213\010\000\000\000\000\000\000\003\313\110\315\311\311\347\002\000\041\060\072\066\006\000\000\000" | "$1" -d >/dev/null' sh "$U"
t "a gzip length error" "" 1 sh -c 'printf "\037\213\010\000\000\000\000\000\000\003\313\110\315\311\311\347\002\000\040\060\072\066\007\000\000\000" | "$1" -d >/dev/null' sh "$U"
t "gzip: trailing garbage" "" 1 sh -c '{ "$1" -c -g hello; echo x; } | "$1" -d >/dev/null' sh "$U"
t "gzip: an unknown method" "" 1 sh -c 'printf "\037\213\007\000\000\000\000\000\000\003" | "$1" -d' sh "$U"
t "lzw: more bits than 16" "" 1 sh -c 'printf "\037\235\221\141" | "$1" -d' sh "$U"
t "lzw: a code not yet made" "" 1 sh -c 'printf "\037\235\220\141\370\007" | "$1" -d >/dev/null' sh "$U"
ok "random damage does not crash" sh -c '"$1" -c -g text > tz; "$1" -c text > tZ; i=0
    for off in 20 50 77 120 300 1000; do for f in tz tZ; do
        cp $f d; printf "\377\000\125" | dd of=d bs=1 seek=$off conv=notrunc 2>/dev/null
        "$1" -d < d > /dev/null 2>&1; [ $? -lt 128 ] || exit 1
    done; done' sh "$U"

# compressing files: each test in a directory of its own
# in_new: cd to a new directory with copies of hello, text and rnd
N=0
in_new() { N=$((N + 1)); mkdir "$T/w$N" && cp "$T/hello" "$T/text" "$T/rnd" "$T/w$N" && cd "$T/w$N"; }
in_new; t "compress file: file.Z, and file is gone" "no yes" 0 sh -c 'cp text f; "$1" f && { [ -e f ] && echo yes || echo no; } | tr "\n" " "; [ -e f.Z ] && echo yes' sh "$U"
in_new; t "uncompress file.Z: file back" "same${nl}no" 0 sh -c 'cp text f; "$1" f && "$2" f.Z && cmp -s f text && echo same; [ -e f.Z ] && echo yes || echo no' sh "$U" "$Z"
in_new; t "compress -d file.Z" "same" 0 sh -c 'cp text f; "$1" f && "$1" -d f.Z && cmp -s f text && echo same' sh "$U"
in_new; t "uncompress -d" "same" 0 sh -c 'cp text f; "$1" f && "$2" -d f.Z && cmp -s f text && echo same' sh "$U" "$Z"
in_new; t "-g: file.gz, and back" "yes${nl}same" 0 sh -c 'cp text f; "$1" -g f && [ -e f.gz ] && echo yes && "$2" f.gz && cmp -s f text && echo same' sh "$U" "$Z"
in_new; t "the mode and times are kept" "yes" 0 sh -c 'cp text f; chmod 640 f; touch -t 200102030405.06 f; m=$(mt f); "$1" f && [ "$(mt f.Z)" = "$m" ] && "$2" f.Z && [ "$(mt f)" = "$m" ] && echo yes' sh "$U" "$Z" 
in_new; t "the access time is kept" "200102030405" 0 sh -c 'cp text f; touch -a -t 200102030405.06 f; "$1" f && TZ=UTC0 find f.Z -newerat "2001-02-03" -prune -o -print 2>/dev/null | sed 1q | grep -q . && echo 200102030405 || { ls -lu f.Z | grep -q " 2001 " && echo 200102030405; }' sh "$U"
in_new; t "uncompress file: file.Z is the input" "same" 0 sh -c 'cp text f; "$1" f && "$2" f && cmp -s f text && echo same' sh "$U" "$Z"
in_new; t "zcat file: file.Z is the input" "hello" 0 sh -c 'cp hello h; "$1" -f h && "$2" h' sh "$U" "$C"
in_new; t "zcat file.Z" "hello" 0 sh -c 'cp hello h; "$1" -f h && "$2" h.Z && [ -e h.Z ]' sh "$U" "$C"
in_new; t "zcat of several files" "hello${nl}hello" 0 sh -c 'cp hello h1; cp hello h2; "$1" -f -g h1 h2; "$2" h1.gz h2.gz' sh "$U" "$C"
in_new; t "uncompress -c: the file is kept" "hello yes" 0 sh -c 'cp hello h; "$1" -f h; "$2" -c h.Z | tr "\n" " "; [ -e h.Z ] && [ ! -e h ] && echo yes' sh "$U" "$Z"
in_new; t "compress -c: no file made, the input kept" "yes" 0 sh -c 'cp text f; "$1" -c f > o && [ -e f ] && [ ! -e f.Z ] && echo yes' sh "$U"
in_new; t "several files" "a.Z b.Z" 0 sh -c 'mkdir s && cd s && cp ../text a && cp ../text b && "$1" a b && echo *' sh "$U"
in_new; t "no operands: the standard input to the standard output" "same" 0 sh -c '"$1" < text | "$2" | cmp -s - text && echo same' sh "$U" "$Z"
in_new; t "-: the standard input at that point" "3002 hello" 0 sh -c '"$1" -c -f -g hello - hello < text | "$2" | { wc -l | tr -d " "; } | tr "\n" " "; "$1" -c -f -g hello - < text | "$2" | sed 1q' sh "$U" "$Z"
in_new; t "-: with files, the files replaced" "same yes" 0 sh -c 'cp text f; cp text g; "$1" f - g < text > o.Z && "$2" < o.Z | cmp -s - text && echo same | tr "\n" " "; [ -e f.Z ] && [ -e g.Z ] && [ ! -e o ] && echo yes' sh "$U" "$Z"
in_new; t "uncompress -: the standard input" "hello" 0 sh -c '"$1" -c hello | "$2" -' sh "$U" "$Z"
in_new; t "a name ending in .gz: expanded to the name without it" "same" 0 sh -c 'cp text t2; "$1" -g t2 && "$2" t2.gz && cmp -s t2 text && echo same' sh "$U" "$Z"

# not compressed: it would grow (status 2)
in_new; t "a file that would grow is left alone" "yes" 2 sh -c 'cp hello h; "$1" h; s=$?; [ -e h ] && [ ! -e h.Z ] && echo yes; exit $s' sh "$U"
in_new; t "-f compresses it anyway" "yes" 0 sh -c 'cp hello h; "$1" -f h && [ -e h.Z ] && [ ! -e h ] && echo yes' sh "$U"
in_new; t "-c: written, status 2" "hello" 2 sh -c '"$1" -c hello > o; s=$?; "$1" -d < o; exit $s' sh "$U"
in_new; t "-c -f: status 0" "" 0 sh -c '"$1" -c -f hello > /dev/null' sh "$U"
in_new; t "random data with -g" "yes" 2 sh -c 'cp rnd r; "$1" -g r; s=$?; [ -e r ] && [ ! -e r.gz ] && echo yes; exit $s' sh "$U"
in_new; t "status 2 for one file, the others done" "f.Z h" 2 sh -c 'mkdir s2 && cd s2 && cp ../text f && cp ../hello h && "$1" h f; s=$?; echo *; exit $s' sh "$U"
in_new; t "an error wins over status 2" "" 1 sh -c 'cp hello h; "$1" h nonexistent' sh "$U"

# errors and the existing output
in_new; t "a missing file: the others are done" "f.Z" 1 sh -c 'mkdir s3 && cd s3 && cp ../text f && "$1" nonexistent f; s=$?; echo *; exit $s' sh "$U"
in_new; t "already .Z: not changed" "yes" 1 sh -c 'cp text a.Z; "$1" a.Z; s=$?; [ -e a.Z ] && [ ! -e a.Z.Z ] && echo yes; exit $s' sh "$U"
in_new; t "already .gz with -g" "yes" 1 sh -c 'cp text a.gz; "$1" -g a.gz; s=$?; [ ! -e a.gz.gz ] && echo yes; exit $s' sh "$U"
in_new; t "but .Z with -g is fine" "yes" 0 sh -c 'cp text b.Z; "$1" -g b.Z && [ -e b.Z.gz ] && echo yes' sh "$U"
in_new; t "a directory" "" 1 "$U" .
in_new; t "a symbolic link without -f" "yes" 1 sh -c 'cp text tl; ln -s tl sl; "$1" sl; s=$?; [ -h sl ] && [ ! -e sl.Z ] && echo yes; exit $s' sh "$U"
in_new; t "a symbolic link with -f: followed" "yes" 0 sh -c 'cp text tl2; ln -s tl2 sl2; "$1" -f sl2 && [ ! -e sl2 ] && [ -f sl2.Z ] && [ -f tl2 ] && echo yes' sh "$U"
in_new; t "more than one link without -f" "yes" 1 sh -c 'cp text l1; ln l1 l2; "$1" l1; s=$?; [ -e l1 ] && [ ! -e l1.Z ] && echo yes; exit $s' sh "$U"
in_new; t "more than one link with -f: this link done" "yes" 0 sh -c 'cp text m1; ln m1 m2; "$1" -f m1 && [ -e m1.Z ] && [ ! -e m1 ] && [ -e m2 ] && echo yes' sh "$U"
in_new; t "the output exists: not overwritten" "same" 1 sh -c 'cp text o; echo old > o.Z; "$1" o < /dev/null; s=$?; [ "$(cat o.Z)" = old ] && cmp -s o text && echo same; exit $s' sh "$U"
in_new; t "the output exists, -f: overwritten" "same" 0 sh -c 'cp text o2; echo old > o2.Z; "$1" -f o2 && "$2" -c o2.Z | cmp -s - text && echo same' sh "$U" "$Z"
in_new; t "uncompress: the output exists" "old" 1 sh -c 'cp hello u; "$1" -f u; echo old > u; "$2" u.Z < /dev/null; s=$?; cat u; exit $s' sh "$U" "$Z"
in_new; t "a terminal: asked, y overwrites" "same" 0 sh -c 'cp text p; echo old > p.Z; PTYRUN_INPUT="y$3" python3 "$TL/ptyrun.py" 02 "$1" p > msg; s=$?; grep -q "p.Z already exists; overwrite" msg && "$2" -c p.Z | cmp -s - text && echo same; exit $s' sh "$U" "$Z" "$nl" 
in_new; t "a terminal: asked, n keeps it" "old" 1 sh -c 'cp text q; echo old > q.Z; PTYRUN_INPUT="n$2" python3 "$TL/ptyrun.py" 0 "$1" q > /dev/null; s=$?; cat q.Z; [ -e q ] && exit $s' sh "$U" "$nl"
in_new; t "a name too long with the suffix" "yes" 1 sh -c 'n=$(awk "BEGIN { while (i++ < 254) printf \"n\" }"); cp text $n || exit 0; "$1" $n; s=$?; [ -e $n ] && echo yes; exit $s' sh "$U"
in_new; t "corrupt input: the input kept, no output left" "yes" 1 sh -c 'echo plain > bad.Z; "$1" bad.Z; s=$?; [ -e bad.Z ] && [ ! -e bad ] && echo yes; exit $s' sh "$U" 
if ! $root; then
    in_new; t "a directory that cannot be written: nothing changed" "yes" 1 sh -c 'mkdir ro && cp text ro/f && chmod 555 ro; "$1" ro/f; s=$?; chmod 755 ro; [ -e ro/f ] && [ ! -e ro/f.Z ] && echo yes; exit $s' sh "$U"
fi

# -v
in_new; t "-v: the percentage" "yes" 0 sh -c 'cp text v; "$1" -v v 2>&1 | grep -E "^v: Compression: [0-9]+\.[0-9][0-9]% -- replaced with v\.Z\$" >/dev/null && echo yes' sh "$U"
in_new; t "-v when expanding" "v.Z: -- replaced with v" 0 sh -c 'cp text v; "$1" v && "$2" -v v.Z 2>&1' sh "$U" "$Z"
in_new; t "-v -c" "yes" 0 sh -c '"$1" -v -c text 2>&1 >/dev/null | grep -E "Compression: [0-9]+\.[0-9][0-9]%\$" >/dev/null && echo yes' sh "$U"
in_new; t "no -v: nothing on standard error" "" 0 sh -c 'cp text w; "$1" w 2>&1' sh "$U"

# options
cd "$T" || exit 2
t "-b 8: out of range for lzw" "" 1 "$U" -c -b 8 text
t "-b 17: out of range for lzw" "" 1 "$U" -c -b 17 text
t "-b 0: out of range for gzip" "" 1 "$U" -c -g -b 0 text
t "-b 10: out of range for gzip" "" 1 "$U" -c -g -b 10 text
t "-b x" "" 1 "$U" -c -b x text
t "-m with an unknown algorithm: status 3, the file kept" "yes" 3 sh -c 'cp text k; "$1" -m bzip2 k; s=$?; [ -e k ] && [ ! -e k.Z ] && echo yes; exit $s' sh "$U"
t "an unknown option" "" 1 "$U" -x text
t "uncompress: an unknown option" "" 2 "$Z" -g
t "zcat takes no options" "" 2 "$C" -c
# getopt() must not take an option after an operand for one (no permuting)
t "an option after an operand is an operand" "hello" 1 sh -c 'cp hello hh; "$1" -f hh; "$2" -c hh.Z -f' sh "$U" "$Z"

done_tests compress
