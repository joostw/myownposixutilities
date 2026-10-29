#!/bin/sh
# gcdiff.sh - compare getconf with the system's (glibc's) getconf: every
# variable it knows: the system variables, and the path variables for a
# few pathnames.  Not compared: the variables glibc's getconf does not
# know, and the minimum values of <limits.h> that it takes from sysconf()
# instead of the fixed values of the standard (_POSIX_ARG_MAX,
# _POSIX_CHILD_MAX, _POSIX_NGROUPS_MAX, _POSIX_OPEN_MAX, _POSIX_STREAM_MAX,
# _POSIX_TZNAME_MAX).
# usage: sh tests/gcdiff.sh ./getconf
G=$(realpath "${1:-./getconf}") || exit 2
src=$(dirname "$G")/getconf.c
pass=0 fail=0 skip=0
names=$(sed -n 's/^    { "\([A-Z_0-9]*\)", .*/\1/p' "$src" | sort -u)
pathvars=$(sed -n '/^static const struct var pathvars/,/^};/s/^    { "\([A-Z_0-9]*\)", .*/\1/p' "$src" | sort -u)
for v in $pathvars; do
    for p in / /tmp /dev/null .; do
        b=$(getconf "$v" "$p" 2>&1); rb=$?
        case $b in *Unrecognized*|*Usage*) skip=$((skip+1)); continue;; esac
        a=$("$G" "$v" "$p" 2>/dev/null); ra=$?
        if [ "$a" = "$b" ] && [ "$ra" = "$rb" ]; then pass=$((pass+1))
        else fail=$((fail+1)); echo "DIFF: getconf $v $p: [$b] $rb, ours [$a] $ra"; fi
    done
done
for v in $names; do
    case " $(echo $pathvars) " in *" $v "*) continue;; esac
    case $v in _POSIX_ARG_MAX|_POSIX_CHILD_MAX|_POSIX_NGROUPS_MAX|_POSIX_OPEN_MAX|_POSIX_STREAM_MAX|_POSIX_TZNAME_MAX)
        skip=$((skip+1)); continue;; esac
    b=$(getconf "$v" 2>&1); rb=$?
    case $b in *Unrecognized*|*Usage*) skip=$((skip+1)); continue;; esac
    a=$("$G" "$v" 2>/dev/null); ra=$?
    if [ "$a" = "$b" ] && [ "$ra" = 0 ]; then pass=$((pass+1))
    else fail=$((fail+1)); echo "DIFF: getconf $v: [$b], ours [$a] $ra"; fi
done
echo "$pass passed, $fail failed ($skip not compared)"
[ "$fail" = 0 ]
