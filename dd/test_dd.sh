#!/bin/sh
# test_dd.sh - tests for dd (POSIX.1-2024).  Run: sh test_dd.sh
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
U=${U:-$PWD/dd}
T=${TMPDIR:-/tmp}/test_dd.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# te NAME EXPECTED-STDERR EXPECTED-STATUS OPERANDS...: dd's standard error
te() {
    _n=$1 _e=$2 _s=$3; shift 3
    _o=$("$U" "$@" 2>&1 >/dev/null); _r=$?
    if [ "$_o" = "$_e" ] && [ "$_r" = "$_s" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s] status %s\n  want: [%s] status %s\n' "$_n" "$_o" "$_r" "$_e" "$_s"; fi
}
# size FILE: its size in bytes
size() { wc -c < "$1" | tr -d ' '; }
recs() { printf '%s+%s records in\n%s+%s records out' "$@"; }

printf 'hello, world\n' > hello
printf '%01000d' 0 > k1           # 1000 bytes
: > empty

# the copy and the counts
t "standard input to standard output" "hello, world" 0 sh -c '"$1" < hello' sh "$U"
te "the counts: one partial block" "$(recs 0 1 0 1)" 0 if=hello
te "the counts: 1000 bytes in 512-byte blocks" "$(recs 1 1 1 1)" 0 if=k1
te "the counts: bs=100" "$(recs 10 0 10 0)" 0 if=k1 bs=100
te "the counts: ibs=300 obs=100" "$(recs 3 1 10 0)" 0 if=k1 ibs=300 obs=100
te "the counts: ibs=100 obs=300" "$(recs 10 0 3 1)" 0 if=k1 ibs=100 obs=300
te "the counts: empty input" "$(recs 0 0 0 0)" 0 if=empty
t "if= and of=" "hello, world" 0 sh -c '"$1" if=hello of=o 2>/dev/null && cat o' sh "$U"
t "\"--\" before the operands" "hello, world" 0 sh -c '"$1" -- if=hello 2>/dev/null' sh "$U"
ok "a large file comes through unchanged" sh -c 'awk "BEGIN { for (i = 0; i < 20000; i++) print i, i * i }" > big; "$1" if=big of=o bs=777 2>/dev/null && cmp -s big o' sh "$U"

# block sizes: expr
te "bs=2k" "$(recs 0 1 0 1)" 0 if=k1 bs=2k
te "bs=1b" "$(recs 1 1 1 1)" 0 if=k1 bs=1b
te "bs=5x20" "$(recs 10 0 10 0)" 0 if=k1 bs=5x20
te "bs=2x5x10" "$(recs 10 0 10 0)" 0 if=k1 bs=2x5x10
te "bs=1kx1" "$(recs 0 1 0 1)" 0 if=k1 bs=1kx1
te "bs= overrides ibs= and obs=" "$(recs 10 0 10 0)" 0 if=k1 ibs=7 obs=9 bs=100
for v in 0 x abc 5x '' 1k2 -1 1x0; do
    t "bs=$v: a usage error" "" 2 "$U" if=hello "bs=$v"
done
t "bs= too large" "" 2 "$U" if=hello bs=99999999999999999999999

# bs= alone writes each read as a block; otherwise blocks are gathered
p2() { { printf abc; sleep 1; printf de; } ; }
ok "bs=10 on a pipe: two short blocks out" sh -c '{ printf abc; sleep 1; printf de; } | "$1" bs=10 2>&1 >/dev/null | grep -q "^0+2 records out"' sh "$U"
ok "ibs=10 obs=10 on a pipe: gathered into one" sh -c '{ printf abc; sleep 1; printf de; } | "$1" ibs=10 obs=10 2>&1 >/dev/null | grep -q "^0+1 records out"' sh "$U"
ok "bs=10 conv=ucase on a pipe: gathered" sh -c '{ printf abc; sleep 1; printf de; } | "$1" bs=10 conv=ucase 2>&1 >/dev/null | grep -q "^0+1 records out"' sh "$U"
ok "bs=10 conv=notrunc on a pipe: not gathered" sh -c '{ printf abc; sleep 1; printf de; } | "$1" bs=10 conv=notrunc 2>&1 >/dev/null | grep -q "^0+2 records out"' sh "$U"
t "iflags=fullblock: one full block" "1+0 records in" 0 sh -c '{ printf abc; sleep 1; printf defghij; } | "$1" bs=10 iflags=fullblock 2>&1 >/dev/null | sed 1q' sh "$U"
t "iflags=fullblock, count= counts full blocks" "ABCDEF" 0 sh -c '{ printf abc; sleep 1; printf def; sleep 1; printf ghi; } | "$1" bs=3 count=2 iflags=fullblock conv=ucase 2>/dev/null' sh "$U"
t "without fullblock count= counts reads" "ab${nl}cdef" 0 sh -c '{ printf ab; sleep 1; printf cdef; } | { "$1" bs=4 count=1 2>/dev/null; echo; cat; }' sh "$U"
t "iflags= other than fullblock" "" 2 "$U" if=hello iflags=direct

# skip=, seek=, count=
printf 'abcdefghijklmnopqrstuvwxyz' > al
t "skip=" "fghijklmnopqrstuvwxyz" 0 "$U" if=al bs=5 skip=1
t "skip= with ibs" "klmnopqrstuvwxyz" 0 "$U" if=al ibs=5 obs=3 skip=2
t "count=" "abcdefghij" 0 "$U" if=al bs=5 count=2
t "skip= and count=" "fghij" 0 "$U" if=al bs=5 skip=1 count=1
t "count=0 copies nothing" "" 0 "$U" if=al count=0
t "skip= on a pipe reads the blocks" "ghijklmnopqrstuvwxyz" 0 sh -c '"$1" bs=2 skip=3 2>/dev/null < al | cat' sh "$U"
t "skip= on a pipe" "pqrstuvwxyz" 0 sh -c 'cat al | "$1" bs=3 skip=5 2>/dev/null' sh "$U"
te "skip= past the end: a warning" "dd: al: cannot skip to specified offset${nl}$(recs 0 0 0 0)" 0 if=al bs=10 skip=3
t "skip= in expr form" "uvwxyz" 0 "$U" if=al bs=1 skip=4x5
t "skip= with a bad number" "" 2 "$U" if=al skip=x
t "count= with a bad number" "" 2 "$U" if=al count=1y
t "seek= keeps the blocks seeked over" "ABCDhello, world" 0 sh -c 'printf ABCDEFGHIJKL > o; "$1" if=hello of=o bs=4 seek=1 2>/dev/null; cat o' sh "$U"
t "seek= truncates after the copy" "17" 0 sh -c 'printf "%040d" 0 > o; "$1" if=hello of=o bs=4 seek=1 2>/dev/null; wc -c < o | tr -d " "' sh "$U"
t "seek= with notrunc keeps the rest" "ABCDhello, world${nl}zzzzzzzzzzz" 0 sh -c 'printf "ABCDEFGHIJKLMNOPQzzzzzzzzzzz" > o; "$1" if=hello of=o bs=4 seek=1 conv=notrunc 2>/dev/null; cat o' sh "$U"
t "seek= and empty input: the size is the offset" "4000" 0 sh -c '"$1" if=empty of=new bs=1000 seek=4 2>/dev/null; wc -c < new | tr -d " "' sh "$U"
t "seek= and empty input: the file is shortened to it" "30" 0 sh -c 'printf "%0100d" 0 > o; "$1" if=empty of=o bs=10 seek=3 2>/dev/null; wc -c < o | tr -d " "' sh "$U"
t "seek= past the end: the hole reads as null bytes" "000 000 000 000 000 000 150 145" 0 sh -c '"$1" if=hello of=new2 bs=3 seek=2 2>/dev/null; od -An -b new2 | sed 1q | cut -d " " -f 2-9' sh "$U"
t "seek= uses obs" "xyz${nl}hello, world" 0 sh -c 'printf "xyzxyzxyz" > o; "$1" if=hello of=o obs=3 ibs=100 seek=1 2>/dev/null; cut -c 1-3 o | sed 1q; tail -c 13 o' sh "$U"
t "seek= on a pipe writes null bytes" "000 000 000 000 150 151" 0 sh -c 'printf hi | "$1" bs=2 seek=2 2>/dev/null | od -An -b | sed 1q | cut -d " " -f 2-' sh "$U"
t "seek= on standard output, a file: from where it is" "xycdehello, world" 0 sh -c 'printf abcdefgh > o; { printf xy; "$1" if=hello bs=3 seek=1 2>/dev/null; } 1<> o; cat o' sh "$U"

# truncation
t "of= truncates the file" "hello, world" 0 sh -c 'printf "%0100d" 0 > o; "$1" if=hello of=o 2>/dev/null; cat o' sh "$U"
t "notrunc: the rest is kept" "hello, world${nl}0000000" 0 sh -c 'printf "%020d" 0 > o; "$1" if=hello of=o conv=notrunc 2>/dev/null; cat o' sh "$U"
t "standard output is not truncated" "hi34567890" 0 sh -c 'printf 1234567890 > o; printf hi | "$1" 2>/dev/null 1<> o; cat o' sh "$U"
t "of=/dev/null" "" 0 "$U" if=hello of=/dev/null
t "the output is made with 0666 less the mask" "-rw-r-----" 0 sh -c 'umask 027; "$1" if=hello of=m 2>/dev/null; ls -l m | cut -c 1-10' sh "$U"

# conversions
printf 'Hello World 123 ABC xyz\n' > mixed
t "conv=ucase" "HELLO WORLD 123 ABC XYZ" 0 "$U" if=mixed conv=ucase
t "conv=lcase" "hello world 123 abc xyz" 0 "$U" if=mixed conv=lcase
t "conv=swab" "badcfe" 0 sh -c 'printf abcdef | "$1" conv=swab 2>/dev/null' sh "$U"
t "conv=swab: an odd last byte stays" "badce" 0 sh -c 'printf abcde | "$1" conv=swab 2>/dev/null' sh "$U"
t "conv=swab within each block" "badcebadce" 0 sh -c 'printf abcdeabcde | "$1" bs=5 conv=swab 2>/dev/null' sh "$U"
t "conv=swab,ucase" "BADC" 0 sh -c 'printf abcd | "$1" conv=swab,ucase 2>/dev/null' sh "$U"
t "conv= given twice adds up" "BADC" 0 sh -c 'printf abcd | "$1" conv=swab conv=ucase 2>/dev/null' sh "$U"
printf 'one\ntwo three\n\nfour' > lines
t "conv=block" "[one     two thre        four    ]" 0 sh -c '"$1" if=lines conv=block cbs=8 2>/dev/null | sed "s/.*/[&]/"' sh "$U"
te "conv=block: the truncated records are counted" "$(recs 0 1 0 1)${nl}1 truncated record" 0 if=lines conv=block cbs=8
te "conv=block: more than one" "$(recs 0 1 0 1)${nl}2 truncated records" 0 if=lines conv=block cbs=3
t "conv=block: the size is records times cbs" "32" 0 sh -c '"$1" if=lines conv=block cbs=8 2>/dev/null | wc -c | tr -d " "' sh "$U"
t "conv=block: records across input blocks" "[one     two thre        four    ]" 0 sh -c '"$1" if=lines conv=block cbs=8 ibs=3 obs=5 2>/dev/null | sed "s/.*/[&]/"' sh "$U"
printf 'one     two thre        four' > fixed
t "conv=unblock" "one${nl}two thre${nl}${nl}four" 0 "$U" if=fixed conv=unblock cbs=8
t "conv=unblock: a short last record" "ab${nl}cd" 0 sh -c 'printf "ab  cd" | "$1" conv=unblock cbs=4 2>/dev/null' sh "$U"
t "conv=unblock: the newlines are added" "4" 0 sh -c '"$1" if=fixed conv=unblock cbs=8 2>/dev/null | wc -l | tr -d " "' sh "$U"
t "conv=unblock: leading spaces stay" "  a${nl}  b" 0 sh -c 'printf "  a  b" | "$1" conv=unblock cbs=3 2>/dev/null' sh "$U"
t "conv=block then unblock: back to the lines" "one${nl}two thre${nl}${nl}four" 0 sh -c '"$1" if=lines conv=block cbs=8 2>/dev/null | "$1" conv=unblock cbs=8 2>/dev/null' sh "$U"
t "conv=block,lcase" "[one     two threfour    ]" 0 sh -c 'printf "ONE\nTWO THREE\nFOUR\n" | "$1" conv=block,lcase cbs=8 2>/dev/null | sed "s/.*/[&]/"' sh "$U"

# sync
t "conv=sync pads with null bytes" "150 151 000 000 000" 0 sh -c 'printf hi | "$1" bs=5 conv=sync 2>/dev/null | od -An -b | sed 1q | cut -d " " -f 2-' sh "$U"
te "conv=sync: the counts" "$(recs 1 1 2 0)" 0 if=k1 conv=sync
t "conv=sync: the size" "1024" 0 sh -c '"$1" if=k1 conv=sync 2>/dev/null | wc -c | tr -d " "' sh "$U"
t "conv=sync with block pads with spaces" "hi________" 0 sh -c 'printf hi | "$1" ibs=5 conv=sync,block cbs=10 2>/dev/null | tr " " _' sh "$U"
t "conv=sync with unblock pads with spaces" "hi" 0 sh -c 'printf hi | "$1" ibs=5 conv=sync,unblock cbs=10 2>/dev/null' sh "$U"
t "conv=sync on a pipe: every read is padded" "10" 0 sh -c '{ printf abc; sleep 1; printf de; } | "$1" ibs=5 conv=sync 2>/dev/null | wc -c | tr -d " "' sh "$U"

# ascii, ebcdic, ibm (XSI)
t "conv=ebcdic: LF is 045" "045" 0 sh -c 'printf "\n" | "$1" conv=ebcdic 2>/dev/null | od -b | sed 1q | awk "{ print \$2 }"' sh "$U"
t "conv=ebcdic: A is 0301, a is 0201, 0 is 0360" "301 201 360" 0 sh -c 'printf Aa0 | "$1" conv=ebcdic 2>/dev/null | od -b | sed 1q | cut -c 9-' sh "$U"
t "conv=ebcdic and ibm differ: ^ is 0232 and 0137" "232 137" 0 sh -c 'printf "^" | "$1" conv=ebcdic 2>/dev/null > a; printf "^" | "$1" conv=ibm 2>/dev/null > b; cat a b | od -b | sed 1q | cut -c 9-' sh "$U"
ok "conv=ebcdic then ascii: all 256 bytes back" sh -c 'awk "BEGIN { for (i = 0; i < 256; i++) printf \"%c\", i }" > all 2>/dev/null; [ "$(wc -c < all)" -eq 256 ] && "$1" if=all conv=ebcdic 2>/dev/null | "$1" conv=ascii 2>/dev/null | cmp -s - all' sh "$U"
ok "conv=ebcdic is one to one" sh -c '"$1" if=all conv=ebcdic 2>/dev/null | od -An -v -b | tr " " "\n" | grep . | sort -u | wc -l | grep -q "^ *256\$"' sh "$U"
t "conv=ebcdic with cbs: blocks first" "301 100 100 302 100 100" 0 sh -c 'printf "A\nB\n" | "$1" conv=ebcdic cbs=3 2>/dev/null | od -b | sed 1q | cut -c 9- | sed "s/ *\$//"' sh "$U"
t "conv=ascii with cbs: unblocks after" "A${nl}B" 0 sh -c 'printf "\301\100\100\302\100\100" | "$1" conv=ascii cbs=3 2>/dev/null' sh "$U"
t "conv=ascii,lcase" "ab" 0 sh -c 'printf "\301\302" | "$1" conv=ascii,lcase 2>/dev/null' sh "$U"

# conflicting conversions, unknown operands
for v in block,unblock lcase,ucase ascii,ebcdic ebcdic,ibm ascii,ibm ascii,block ebcdic,unblock foo ucase,
do
    t "conv=$v: a usage error" "" 2 "$U" if=hello conv=$v cbs=4
done
t "an unknown operand" "" 2 "$U" if=hello size=3
t "an operand without =" "" 2 "$U" hello
t "an option" "" 2 "$U" -x
t "nothing read before the operands are checked" "x" 0 sh -c 'echo x | { "$1" bs=1 count=1 bogus=1 2>/dev/null; cat; }' sh "$U"

# errors
t "a missing input file" "" 1 "$U" if=nonexistent
t "no output file made when the input is missing" "no" 0 sh -c '"$1" if=nonexistent of=made 2>/dev/null; [ -e made ] || echo no' sh "$U"
t "an output in a missing directory" "" 1 "$U" if=hello of=no/such
te "a read error: the message and the counts" "dd: .: Is a directory${nl}$(recs 0 0 0 0)" 1 if=.
t "a write error" "" 1 sh -c '"$1" if=hello of=/dev/full' sh "$U"
te "noerror: the counts after each error" "dd: .: Is a directory${nl}$(recs 0 0 0 0)${nl}dd: .: Is a directory${nl}$(recs 0 0 0 0)${nl}$(recs 0 0 0 0)" 0 if=. count=2 conv=noerror
te "noerror,sync: null bytes for the bad blocks" "dd: .: Is a directory${nl}$(recs 0 0 0 0)${nl}dd: .: Is a directory${nl}$(recs 0 1 1 0)${nl}$(recs 0 2 2 0)" 0 if=. count=2 bs=4 conv=noerror,sync
t "noerror,sync: the output" "8" 0 sh -c '"$1" if=. count=2 bs=4 conv=noerror,sync 2>/dev/null | od -An -b | tr " " "\n" | grep -c "^000\$"' sh "$U"

# signals
# python3 -c "$DFL" COMMAND...: COMMAND with SIGINT at its default (sh
# ignores it for "&")
DFL='import os, signal, sys; signal.signal(signal.SIGINT, signal.SIG_DFL); os.execv(sys.argv[1], sys.argv[1:])'
export DFL
ok "SIGINT: the counts, then killed by SIGINT" sh -c '
    { printf abc; sleep 3; } | { exec 3<&0; python3 -c "$DFL" "$1" bs=1 <&3 > o 2> e & p=$!; sleep 1; kill -INT $p; wait $p; echo $? > st; }
    grep -q "^3+0 records in\$" e && [ "$(cat st)" -gt 128 ] && [ "$(kill -l "$(cat st)")" = INT ]' sh "$U"
ok "SIGUSR1: the counts, and the copy goes on" sh -c '
    { printf abc; sleep 2; printf de; } | { exec 3<&0; "$1" bs=1 <&3 > o 2> e & p=$!; sleep 1; kill -USR1 $p; wait $p; echo $? > st; }
    [ "$(cat st)" = 0 ] && [ "$(cat o)" = abcde ] && grep -c "records in" e | grep -q 2 && grep -q "^3+0 records in\$" e' sh "$U"
ok "SIGTERM: default action, no counts" sh -c '
    { sleep 3; } | { exec 3<&0; "$1" bs=1 <&3 > o 2> e & p=$!; sleep 1; kill -TERM $p; { wait $p; } 2>/dev/null; echo $? > st; }
    [ ! -s e ] && [ "$(kill -l "$(cat st)")" = TERM ]' sh "$U"

done_tests dd
