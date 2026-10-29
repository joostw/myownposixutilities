#!/bin/sh
# test_touch.sh - tests for touch (POSIX.1-2024).  Run: sh test_touch.sh
cd "$(dirname "$0")" || exit 2
pass=0 fail=0
# t NAME EXPECTED-STDOUT EXPECTED-STATUS COMMAND...  (stdout compared without trailing newlines)
t() {
    _n=$1 _e=$2 _s=$3; shift 3
    _o=$("$@" 2>/dev/null); _r=$?
    if [ "$_o" = "$_e" ] && [ "$_r" = "$_s" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s] status %s\n  want: [%s] status %s\n' "$_n" "$_o" "$_r" "$_e" "$_s"; fi
}
# tb NAME EXPECTED-OD-C COMMAND...  (exact bytes of stdout, as od -c)
tb() {
    _n=$1 _e=$2; shift 2
    _o=$("$@" 2>/dev/null | od -An -c | tr -s ' \n' ' ' | sed 's/^ //;s/ $//')
    if [ "$_o" = "$_e" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  %s\n  want: %s\n' "$_n" "$_o" "$_e"; fi
}
# ok NAME CONDITION-COMMAND...
ok() {
    _n=$1; shift
    if "$@"; then pass=$((pass+1)); else fail=$((fail+1)); printf 'FAIL %s\n' "$_n"; fi
}
done_tests() { echo "$1: $pass passed, $fail failed"; [ "$fail" = 0 ]; }
U=${U:-$PWD/touch}
T=${TMPDIR:-/tmp}/test_touch.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
# fstat: exact file status, which POSIX ls does not show (../testlib/fstat.c)
cc=${CC:-}
[ -n "$cc" ] || for c in c17 c99 cc; do command -v $c >/dev/null 2>&1 && { cc=$c; break; }; done
trap 'rm -rf "$T" "$T.fstat"' EXIT
$cc -o "$T.fstat" ../testlib/fstat.c || { echo "cannot compile ../testlib/fstat.c"; exit 2; }
fstat() { "$T.fstat" "$@"; }
mt() { TZ=UTC0 fstat %y "$1" | cut -c1-19; }
at() { TZ=UTC0 fstat %x "$1" | cut -c1-19 | tr ' ' .; }
ns() { fstat %y "$1" | sed 's/.*\.\([0-9]*\).*/\1/'; }

t create "" 0 "$U" "$T/new"
ok "created" test -f "$T/new"
t "-c no create" "" 0 "$U" -c "$T/none"
ok "-c did not create" test ! -e "$T/none"

t "-t CCYY" "" 0 env TZ=UTC0 "$U" -t 202001021304.05 "$T/a"
t "-t result" "2020-01-02 13:04:05" 0 mt "$T/a"
t "-t YY 69 -> 1969" "" 0 env TZ=UTC0 "$U" -t 6912311200 "$T/a"
t "-t 69 result" "1969-12-31 12:00:00" 0 mt "$T/a"
t "-t YY 68 -> 2068" "" 0 env TZ=UTC0 "$U" -t 6801011200 "$T/a"
t "-t 68 result" "2068-01-01 12:00:00" 0 mt "$T/a"
t "-t SS=60" "" 0 env TZ=UTC0 "$U" -t 202001010000.60 "$T/a"
t "-t SS=60 result" "2020-01-01 00:01:00" 0 mt "$T/a"
t "-t TZ affects" "" 0 env TZ=EST5 "$U" -t 202001010000 "$T/a"
t "-t TZ result" "2020-01-01 05:00:00" 0 mt "$T/a"
t "-t bad month" "" 1 "$U" -t 202013010000 "$T/a"
t "-t 31 Feb" "" 1 "$U" -t 202002310000 "$T/a"
t "-t 29 Feb leap" "" 0 env TZ=UTC0 "$U" -t 202002291200 "$T/a"
t "-t SS 61" "" 1 "$U" -t 202001010000.61 "$T/a"
t "-t short" "" 1 "$U" -t 0101000 "$T/a"
t "-t letters" "" 1 "$U" -t 20200101x000 "$T/a"
t "-t no-error leaves file" "2020-02-29 12:00:00" 0 mt "$T/a"

t "-d T Z" "" 0 "$U" -d 2021-03-04T05:06:07Z "$T/a"
t "-d result" "2021-03-04 05:06:07" 0 mt "$T/a"
t "-d space local" "" 0 env TZ=UTC0 "$U" -d "2021-03-04 05:06:08" "$T/a"
t "-d space result" "2021-03-04 05:06:08" 0 mt "$T/a"
t "-d frac" "" 0 "$U" -d 2021-03-04T05:06:07.5Z "$T/a"
t "-d frac ns" "500000000" 0 ns "$T/a"
t "-d comma frac" "" 0 "$U" -d 2021-03-04T05:06:07,25Z "$T/a"
t "-d comma ns" "250000000" 0 ns "$T/a"
t "-d 5-digit year" "" 0 "$U" -d 12021-01-01T00:00:00Z "$T/b"
t "-d empty frac" "" 1 "$U" -d 2021-03-04T05:06:07.Z "$T/a"
t "-d 3-digit year" "" 1 "$U" -d 999-03-04T05:06:07Z "$T/a"
t "-d other tz" "" 1 "$U" -d 2021-03-04T05:06:07+01 "$T/a"
t "-d 31 Apr" "" 1 "$U" -d 2021-04-31T00:00:00Z "$T/a"

t "ref set" "" 0 "$U" -d 2000-01-01T00:00:00Z "$T/ref"
t "-r" "" 0 "$U" -r "$T/ref" "$T/a"
t "-r result" "2000-01-01 00:00:00" 0 mt "$T/a"
t "-r missing" "" 1 "$U" -r "$T/nope" "$T/a"
t "-r and -t" "" 1 "$U" -r "$T/ref" -t 202001010000 "$T/a"
t "-t and -d" "" 1 "$U" -t 202001010000 -d 2021-03-04T05:06:07Z "$T/a"

t "both set" "" 0 "$U" -d 2010-01-01T00:00:00Z "$T/c"
t "-a only" "" 0 "$U" -a -d 2011-01-01T00:00:00Z "$T/c"
t "-a: mtime kept" "2010-01-01 00:00:00" 0 mt "$T/c"
t "-a: atime set" "2011-01-01.00:00:00" 0 at "$T/c"
t "-m only" "" 0 "$U" -m -d 2012-01-01T00:00:00Z "$T/c"
t "-m: mtime set" "2012-01-01 00:00:00" 0 mt "$T/c"
t "-m: atime kept" "2011-01-01.00:00:00" 0 at "$T/c"

t "no operand" "" 1 "$U"
t "bad option" "" 1 "$U" -x "$T/a"
t "unwritable dir" "" 1 "$U" "$T/nodir/x"
t "error continues" "" 1 "$U" "$T/nodir/x" "$T/after"
ok "later operand done" test -f "$T/after"
t "-- operand" "" 0 sh -c 'cd "$1" && "$2" -- -f && test -f ./-f' sh "$T" "$U"
t "options after operand are operands" "" 0 sh -c 'cd "$1" && "$2" zz -c && test -f zz && test -f ./-c' sh "$T" "$U"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: touch g -c: -c is a file" "-c
f
g" 0 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" g -c && ls; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests touch
