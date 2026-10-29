#!/bin/sh
# prdiff.sh - compare pr with GNU pr ("pr" in PATH, with POSIXLY_CORRECT
# in the C locale): the standard output for many options on a few inputs.
# Normalized first: tabs expanded, trailing blanks removed, and the header
# line reduced to its file and page (GNU centers the file; the standard
# has single spaces).  Not compared: +page past the last page (GNU warns),
# and -m -n with a last line without a newline (GNU numbers it oddly).
# usage: sh tests/prdiff.sh ./pr
P=$(realpath "${1:-./pr}") || exit 2
W=${TMPDIR:-/tmp}/prdiff.$$
mkdir -m 700 "$W" || exit 2
trap 'rm -rf "$W"' EXIT
cd "$W" || exit 2
export LC_ALL=C POSIXLY_CORRECT=1 TZ=UTC0
awk 'BEGIN { for (i = 1; i <= 200; i++) print i }' > n200
printf 'abcdefghijklmnopqrstuvwxyz\nb\tx\nc\n\nd\ne\tf\tg\nf\ng\n' > pc
awk 'BEGIN { for (i = 1; i <= 120; i++) printf "line %d %s\n", i, substr("the quick brown fox jumps over the lazy dog", 1, i % 44) }' > txt
: > empty
printf 'no newline' > nonl
norm() { expand | sed 's/ *$//' | sed -E 's/^.*[0-9]{2}:[0-9]{2} [0-9]{4} +(.*) +Page ([0-9]+)$/HEADER \1 Page \2/; s/ +Page / Page /'; }
pass=0 fail=0
for o in "" -t -2 -3 -4 -5 "-2 -a" "-3 -a" -d "-2 -d" -n "-n -3" "-n:3" "-n, -2" "-n -2 -w 40" -e -e4 -ex4 -i -i4 \
         "-l 20" "-l 10" "-l 5" "-l 11" "-w 40 -3" "-w 120 -4" "-s -3" "-s: -2" "-o 4" "-o 4 -3" "+2 -l 20" \
         "-h HDR" -F "-F -3" "-t -3" "-t -3 -a" "-2 -t -l 7" -m "-m -t" "-m -n" "-m -s" "-m -w 50"; do
    for file in n200 pc txt empty nonl; do
        case $o in *-m*) args="$file pc txt";; *) args=$file;; esac
        case "$o $file" in "-m -n nonl"|"+2 -l 20 empty"|"+2 -l 20 nonl"|"+2 -l 20 pc") continue;; esac
        a=$("$P" $o $args 2>/dev/null | norm)
        b=$(pr $o $args 2>/dev/null | norm)
        if [ "$a" = "$b" ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "DIFF: pr $o $args"; fi
    done
done
echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
