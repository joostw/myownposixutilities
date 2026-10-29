#!/bin/sh
# tabsdiff.sh - compare tabs with ncurses tabs ("tabs" in PATH): the output
# and the exit status, for every terminal type of the system's terminfo
# directories, every option and some lists.  Not compared: a list of one
# value (ncurses takes it as an interval), and lists that are not valid
# (ncurses takes them).
# usage: sh tests/tabsdiff.sh ./tabs
B=$(realpath "${1:-./tabs}") || exit 2
W=${TMPDIR:-/tmp}/tabsdiff.$$
mkdir -m 700 "$W" || exit 2
trap 'rm -rf "$W"' EXIT
pass=0 fail=0
terms=$(for d in /etc/terminfo /lib/terminfo /usr/share/terminfo; do
    [ -d "$d" ] && ls "$d"/*/ 2>/dev/null | grep -v '^/' | grep -v '^$'; done | sort -u)
for t in $terms; do
    for o in "" -0 -1 -2 -3 -4 -5 -6 -7 -8 -9 -a -a2 -c -c2 -c3 -f -p -s -u 1,10 "1 10 +10 +10" \
             5,6,7 3,+4,+4 "1,90" 10,20,30,40,50,60,70,79,80,81; do
        "$B" -T "$t" $o > "$W/a" 2>/dev/null; ra=$?
        tabs -T "$t" $o > "$W/b" 2>/dev/null; rb=$?
        if cmp -s "$W/a" "$W/b" && [ "$ra" = "$rb" ]; then pass=$((pass+1))
        else fail=$((fail+1)); echo "DIFF: tabs -T $t $o"; fi
    done
done
echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
