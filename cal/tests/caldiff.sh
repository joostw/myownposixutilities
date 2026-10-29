#!/bin/sh
# caldiff.sh - compare cal with busybox cal (BSD's format): every year
# from 1 to 9999, and every month of some years.  Lines are compared
# without trailing blanks, and empty lines are left out (busybox pads its
# months to six weeks).
# usage: sh tests/caldiff.sh ./cal
C=$(realpath "${1:-./cal}") || exit 2
command -v busybox >/dev/null 2>&1 || { echo "caldiff: no busybox"; exit 2; }
W=${TMPDIR:-/tmp}/caldiff.$$
mkdir -m 700 "$W" || exit 2
trap 'rm -rf "$W"' EXIT
cd "$W" || exit 2
export LC_ALL=C
norm() { sed 's/ *$//' | grep -v '^$'; }
pass=0 fail=0
y=1
while [ $y -le 9999 ]; do
    "$C" $y | norm > a; busybox cal $y | norm > b
    if cmp -s a b; then pass=$((pass+1)); else fail=$((fail+1)); echo "DIFF: cal $y"; fi
    y=$((y+1))
done
for y in 1 4 100 1582 1600 1700 1751 1752 1753 1800 1900 2000 2024 2100 9999; do
    for m in 1 2 3 4 5 6 7 8 9 10 11 12; do
        "$C" $m $y | norm > a; busybox cal $m $y | norm > b
        if cmp -s a b; then pass=$((pass+1)); else fail=$((fail+1)); echo "DIFF: cal $m $y"; fi
    done
done
echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
