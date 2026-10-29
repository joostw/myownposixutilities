#!/bin/sh
# run_tests.sh - build every utility and run its tests (<utility>/test_<utility>.sh).
#
# Usage: sh run_tests.sh [utility...]
#
# Each test script ends with "<utility>: N passed, M failed"; that line is
# shown for each utility, with the details of whatever failed, and then
# the totals.  The exit status is 0 if everything passed.
#
#   CC=...   rebuild with another compiler command first ("make clean"
#            and "make CC=..."), for example with sanitizers:
#            ASAN_OPTIONS=detect_leaks=0 \
#            CC="cc -fsanitize=address,undefined -g" sh run_tests.sh
#   MAKE=... the make to build with (default: make)
cd "$(dirname "$0")" || exit 2
MAKE=${MAKE:-make}

if [ $# -gt 0 ]; then
    list=$*
else
    list=$(sed -n '/^UTILS *=/,/[^\\]$/p' Makefile | sed 's/^UTILS *=//; s/\\$//')
fi

log=${TMPDIR:-/tmp}/run_tests.$$
: > "$log" || exit 2
trap 'rm -f "$log"' EXIT
failed= total_pass=0 total_fail=0
for u in $list; do
    if [ ! -f "$u/test_$u.sh" ]; then
        echo "$u: no test_$u.sh"
        failed="$failed $u"
        continue
    fi
    if [ -n "$CC" ]; then
        (cd "$u" && $MAKE -s clean && $MAKE -s CC="$CC") > "$log" 2>&1
    else
        (cd "$u" && $MAKE -s) > "$log" 2>&1
    fi
    if [ $? != 0 ]; then
        echo "$u: BUILD FAILED"
        sed 's/^/    /' "$log"
        failed="$failed $u"
        continue
    fi
    out=$(sh "$u/test_$u.sh" 2>&1)
    rc=$?
    summary=$(printf '%s\n' "$out" | tail -n 1)
    echo "$summary"
    if [ $rc != 0 ]; then
        printf '%s\n' "$out" | sed '$d' | sed 's/^/    /'
        failed="$failed $u"
    fi
    p=$(printf '%s\n' "$summary" | sed -n 's/.*: \([0-9]*\) passed, \([0-9]*\) failed$/\1/p')
    f=$(printf '%s\n' "$summary" | sed -n 's/.*: \([0-9]*\) passed, \([0-9]*\) failed$/\2/p')
    total_pass=$((total_pass + ${p:-0}))
    total_fail=$((total_fail + ${f:-1}))
done

echo "total: $total_pass passed, $total_fail failed"
if [ -n "$failed" ]; then
    echo "failing:$failed"
    exit 1
fi
exit 0
