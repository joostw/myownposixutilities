#!/bin/sh
# test_sh.sh - tests for sh (POSIX.1-2024).  Run: sh test_sh.sh
#
# The suites, with their data in tests/ (see README.md):
#   regression  tests/cases*.txt, expected results in tests/expected.json
#   terminal    interactive use on a pseudo-terminal
# Each suite ends with a line "N passed, M failed"; this script adds them up.

cd "$(dirname "$0")" || exit 2
pass=0 fail=0

# suite NAME COMMAND: run one suite, show what failed, count the results
suite() {
    _n=$1
    _o=$(eval "$2" 2>&1)
    _r=$?
    _l=$(printf '%s\n' "$_o" | tail -n 1)
    _p=$(printf '%s\n' "$_l" | sed -n 's/^\([0-9]*\) passed, \([0-9]*\) failed$/\1/p')
    _f=$(printf '%s\n' "$_l" | sed -n 's/^\([0-9]*\) passed, \([0-9]*\) failed$/\2/p')
    if [ -z "$_p" ]; then
        _p=0 _f=1
        _l="did not finish"
    elif [ "$_r" != 0 ] && [ "$_f" = 0 ]; then
        _f=1
    fi
    if [ "$_f" != 0 ]; then
        printf '%s\n' "$_o" | grep -v '^ok ' | sed '$d' | sed "s/^/  [$_n] /"
    fi
    echo "  $_n: $_l"
    pass=$((pass + _p))
    fail=$((fail + _f))
}

[ -x ./sh ] || { echo "sh: not built (run make first)"; exit 2; }

suite regression 'python3 tests/regress.py'
suite terminal 'python3 tests/pty_test.py'

echo "sh: $pass passed, $fail failed"
[ "$fail" = 0 ]
