#!/bin/sh
# oddiff.sh - compare od with GNU od (as "od" in PATH): the standard output
# and the exit status, for many options on a few inputs.
# usage: sh tests/oddiff.sh ./od
O=$(realpath "${1:-./od}") || exit 2
W=${TMPDIR:-/tmp}/oddiff.$$
mkdir -m 700 "$W" || exit 2
trap 'rm -rf "$W"' EXIT
cd "$W" || exit 2
export LC_ALL=C
printf 'Hello, world!\n\0\377\200abc' > a
awk 'BEGIN { srand(3); for (i = 0; i < 1000; i++) printf "%c", int(rand() * 256) }' > r
awk 'BEGIN { for (i = 0; i < 200; i++) printf "x" }' > rep
: > e
printf 'abcdefghijklmnopqrstuvwxyz0123456789' > al
pass=0 fail=0
for f in a r rep e al "a al" "e a"; do
    for opts in "" "-v" "-A d" "-A x" "-A n" "-A o" "-t x1" "-t o1" "-t d1" "-t u1" \
                "-t x2" "-t d2" "-t u2" "-t o4" "-t d4" "-t u4" "-t x4" "-t x8" "-t d8" \
                "-t u8" "-t o8" "-t f4" "-t f8" "-t fL" "-t a" "-t c" "-c" "-b" "-d" "-o" \
                "-s" "-x" "-t x1 -t o2" "-t d1u4" "-t a -t x1 -c" "-j 5" "-j 0x10" "-j 1b" \
                "-N 7" "-N 0x11" "-j 3 -N 20" "-t xC -t dS -t uI -t oL" "-t fF -t fD" \
                "-A x -t x1 -j 17 -N 3" "-bcx" "-t f4 -t x1" "-t fL -t a" "-v -t x1" "-j 2000"; do
        a=$(od $opts $f 2>/dev/null); ra=$?
        b=$("$O" $opts $f 2>/dev/null); rb=$?
        if [ "$a" = "$b" ] && [ "$ra" = "$rb" ]; then
            pass=$((pass+1))
        else
            fail=$((fail+1)); echo "DIFF: od $opts $f (status $ra, ours $rb)"
        fi
    done
done
echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
