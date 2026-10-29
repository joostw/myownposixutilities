#!/bin/sh
# dddiff.sh - compare dd with GNU dd: the output and the counts.
# Run: sh tests/dddiff.sh ./dd   (needs GNU coreutils dd as "dd" in PATH)
# The inputs are regular files, so that each read returns a full block
# where it can.  GNU dd's "bytes copied" line is dropped.  swab is only
# tried with even block sizes: GNU carries an odd byte over to the next
# block, where the standard swaps within each block.
D=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
dd --version 2>/dev/null | grep -q coreutils || { echo "dddiff: GNU dd is needed"; exit 2; }
T=${TMPDIR:-/tmp}/dddiff.$$
mkdir "$T" || exit 2
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
pass=0 fail=0
awk 'BEGIN { srand(7); for (i = 0; i < 60; i++) { n = int(rand() * 23); s = ""
    for (j = 0; j < n; j++) s = s sprintf("%c", 32 + int(rand() * 95)); print s } }' > text
printf 'no newline at the end' > nonl
printf 'Mixed Case Text\nWITH LINES   \n  and spaces  \n' > mixed
awk 'BEGIN { srand(3); for (i = 0; i < 3000; i++) printf "%c", int(rand() * 256) }' > bin 2>/dev/null
: > empty
printf 'abcdefghij      klmno     pqrst' > recs
# c NAME OPERANDS...: if= each input, standard output and standard error
c() {
    _n=$1; shift
    for _f in text nonl mixed bin empty recs; do
        "$D" if=$_f "$@" > o1 2> e1; echo $? >> e1
        dd if=$_f "$@" > o2 2> e2; echo $? >> e2
        grep -v 'copied' e2 > e2a
        if cmp -s o1 o2 && cmp -s e1 e2a; then pass=$((pass+1))
        else fail=$((fail+1)); echo "FAIL $_n ($_f): $*"; diff e1 e2a | sed 's/^/  /' | head -6; fi
    done
}
c "default" 
c "bs=1" bs=1
c "bs=7" bs=7
c "bs=10 count=3" bs=10 count=3
c "ibs=3 obs=5" ibs=3 obs=5
c "ibs=10 obs=4" ibs=10 obs=4
c "ibs=4 skip=3 count=2" ibs=4 skip=3 count=2
c "ibs=4 obs=6 skip=1" ibs=4 obs=6 skip=1
c "skip in expr form" bs=2x3 skip=2
c "bs with k" bs=1k
c "bs with b" bs=1b
c "count=0" count=0
c "swab" conv=swab
c "swab bs=8" bs=8 conv=swab
c "swab ibs=4 obs=6" ibs=4 obs=6 conv=swab
c "lcase" conv=lcase
c "ucase" conv=ucase
c "ucase bs=5" bs=5 conv=ucase
c "block cbs=10" conv=block cbs=10
c "block cbs=3: truncated" conv=block cbs=3
c "block cbs=1" conv=block cbs=1
c "block cbs=40 obs=7" conv=block cbs=40 obs=7
c "unblock cbs=5" conv=unblock cbs=5
c "unblock cbs=16" conv=unblock cbs=16
c "unblock cbs=1" conv=unblock cbs=1
c "block,ucase" conv=block,ucase cbs=8
c "unblock,lcase" conv=unblock,lcase cbs=8
c "sync ibs=7" conv=sync ibs=7
c "sync bs=7" conv=sync bs=7
c "sync ibs=7 obs=3" conv=sync ibs=7 obs=3
c "sync,block" conv=sync,block cbs=5 ibs=7
c "sync,unblock" conv=sync,unblock cbs=5 ibs=7
c "ebcdic" conv=ebcdic
c "ibm" conv=ibm
c "ascii" conv=ascii
c "ebcdic cbs=10" conv=ebcdic cbs=10
c "ibm cbs=10" conv=ibm cbs=10
c "ascii cbs=10" conv=ascii cbs=10
c "ebcdic,lcase cbs=6" conv=ebcdic,lcase cbs=6
c "ascii,ucase cbs=6" conv=ascii,ucase cbs=6
c "two conv operands" conv=ucase conv=swab bs=4
c "bs=6 with notrunc" bs=6 conv=notrunc
c "bs=6 with noerror" bs=6 conv=noerror
c "fullblock" bs=9 iflag=fullblock

# o NAME OPERANDS...: of=out, made from "before" first; compare the file
o() {
    _n=$1; shift
    for _f in text bin empty; do
        printf 'previous contents of the output file, longer than some inputs\n' > out
        "$D" if=$_f of=out "$@" 2> e1; echo $? >> e1; mv out o1
        printf 'previous contents of the output file, longer than some inputs\n' > out
        dd if=$_f of=out "$@" 2> e2; echo $? >> e2; mv out o2
        grep -v 'copied' e2 > e2a
        if cmp -s o1 o2 && cmp -s e1 e2a; then pass=$((pass+1))
        else fail=$((fail+1)); echo "FAIL $_n ($_f): $*"; cmp o1 o2 | sed 's/^/  /'; fi
    done
}
o "of= truncates"
o "notrunc" conv=notrunc
o "seek" bs=4 seek=3
o "seek, notrunc" bs=4 seek=3 conv=notrunc
o "seek past the end" bs=10 seek=20
o "seek past the end, notrunc" bs=10 seek=20 conv=notrunc
o "obs and seek" obs=3 seek=2 ibs=5
echo "dddiff: $pass passed, $fail failed"
[ "$fail" = 0 ]
