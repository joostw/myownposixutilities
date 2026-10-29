#!/bin/sh
# iddiff.sh - compare id with the system's id (GNU): for every user of the
# user database, -u, -un, -g, -gn, the groups of -G and -Gn (as sets: the
# order of the other groups may differ), and the default output; and the
# same for the process.
# usage: sh tests/iddiff.sh ./id
I=$(realpath "${1:-./id}") || exit 2
export LC_ALL=C
pass=0 fail=0
set_of() { tr ' ' '\n' | sort | tr '\n' ' '; }
cmp1() {
    if [ "$1" = "$2" ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "DIFF: id $3: [$2], ours [$1]"; fi
}
users=$(cut -d: -f1 /etc/passwd)
for u in "" $users; do
    for o in -u -un -g -gn -ur -gr; do
        cmp1 "$("$I" $o $u 2>&1)" "$(id $o $u 2>&1)" "$o $u"
    done
    cmp1 "$("$I" -G $u | set_of)" "$(id -G $u | set_of)" "-G $u"
    cmp1 "$("$I" -Gn $u | set_of)" "$(id -Gn $u | set_of)" "-Gn $u"
    cmp1 "$("$I" $u | sed 's/ groups=.*//')" "$(id $u | sed 's/ groups=.*//')" "$u (uid= gid=)"
    cmp1 "$("$I" $u | sed 's/.*groups=//' | tr , '\n' | sort)" "$(id $u | sed 's/.*groups=//' | tr , '\n' | sort)" "$u (groups=)"
done
echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
