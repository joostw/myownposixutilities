#!/bin/sh
# test_date.sh - tests for date (POSIX.1-2024).  Run: sh test_date.sh
cd "$(dirname "$0")" || exit 2
pass=0 fail=0
# t NAME EXPECTED-STDOUT EXPECTED-STATUS COMMAND...  (stdout compared without trailing newlines)
t() {
    _n=$1 _e=$2 _s=$3; shift 3
    _o=$("$@" 2>/dev/null); _r=$?
    if [ "$_o" = "$_e" ] && [ "$_r" = "$_s" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s] status %s\n  want: [%s] status %s\n' "$_n" "$_o" "$_r" "$_e" "$_s"; fi
}
# ok NAME CONDITION-COMMAND...
ok() {
    _n=$1; shift
    if "$@"; then pass=$((pass+1)); else fail=$((fail+1)); printf 'FAIL %s\n' "$_n"; fi
}
done_tests() { echo "$1: $pass passed, $fail failed"; [ "$fail" = 0 ]; }
U=${U:-$PWD/date}
T=${TMPDIR:-/tmp}/test_date.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# the reference: Python's time.strftime() for the time date gives with %s
# (one call of date, so no second can pass between them)
cat > ref.py <<'PY'
import os, sys, time
tz, fmt, out = sys.argv[1], sys.argv[2], sys.stdin.read()
if tz == "-":
    os.environ.pop("TZ", None)
else:
    os.environ["TZ"] = tz
time.tzset()
secs, rest = out.split(" ", 1)
want = time.strftime(fmt, time.localtime(int(secs))) + "\n"
print("same" if rest == want else "differs: %r %r" % (rest, want))
PY
# r NAME TZ FORMAT: date "+%s FORMAT" in TZ, against Python
r() {
    _n=$1 _tz=$2 _f=$3
    if [ "$_tz" = - ]; then
        t "$_n" "same" 0 sh -c 'unset TZ; "$1" "+%s $2" | python3 ref.py - "$2"' sh "$U" "$_f"
    else
        t "$_n" "same" 0 sh -c 'TZ=$3 "$1" "+%s $2" | python3 ref.py "$3" "$2"' sh "$U" "$_f" "$_tz"
    fi
}
all='%a|%A|%b|%B|%c|%C|%d|%D|%e|%F|%g|%G|%h|%H|%I|%j|%m|%M|%n|%p|%r|%R|%S|%t|%T|%u|%U|%V|%w|%W|%x|%X|%y|%Y|%z|%Z|%%'
r "every conversion, UTC0" UTC0 "$all"
r "every conversion, EST5EDT" EST5EDT "$all"
r "every conversion, a half-hour offset" "<+0530>-5:30" "$all"
r "every conversion, a zone with daylight saving time" "CET-1CEST,M3.5.0,M10.5.0/3" "$all"
r "every conversion, TZ unset" - "$all"
r "the modifiers E and O" UTC0 '%Ec|%EC|%Ex|%EX|%Ey|%EY|%Od|%Oe|%OH|%OI|%Om|%OM|%OS|%Ou|%OU|%OV|%Ow|%OW|%Oy'
r "text around the conversions" UTC0 'year %Y, day %j: done'
# -u
t "-u: UTC" "same" 0 sh -c 'TZ=EST5EDT "$1" -u "+%s %H:%M %Z" | python3 ref.py UTC0 "%H:%M %Z"' sh "$U"
t "-u: the timezone UTC" "UTC +0000" 0 sh -c 'TZ=EST5EDT "$1" -u "+%Z %z"' sh "$U"
t "TZ: its offset" "-0500 EST" 0 sh -c 'TZ=EST5 "$1" "+%z %Z"' sh "$U"
t "-u and TZ=UTC0 agree" "same" 0 sh -c 'a=$("$1" -u "+%Y-%m-%d %H"); b=$(TZ=UTC0 "$1" "+%Y-%m-%d %H"); c=$("$1" -u "+%Y-%m-%d %H"); [ "$a" = "$b" ] || [ "$b" = "$c" ] && echo same' sh "$U"
# the default format
t "the default format: %a %b %e %H:%M:%S %Z %Y" "same" 0 sh -c 'for i in 1 2 3; do a=$("$1"); b=$("$1" "+%a %b %e %H:%M:%S %Z %Y"); [ "$a" = "$b" ] && { echo same; exit; }; done' sh "$U"
t "the default format: the fields" "yes" 0 sh -c 'TZ=UTC0 "$1" | grep -E "^(Sun|Mon|Tue|Wed|Thu|Fri|Sat) (Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 123][0-9] [0-2][0-9]:[0-5][0-9]:[0-6][0-9] UTC [0-9]{4}\$" >/dev/null && echo yes' sh "$U"
t "the default format, -u" "yes" 0 sh -c '"$1" -u | grep -E " UTC [0-9]{4}\$" >/dev/null && echo yes' sh "$U"
t "as the system's date, to the minute" "same" 0 sh -c 'for i in 1 2 3; do a=$(TZ=UTC0 "$1" "+%Y%m%d%H%M"); b=$(TZ=UTC0 date "+%Y%m%d%H%M"); [ "$a" = "$b" ] && { echo same; exit; }; done' sh "$U"
t "%s: the seconds of time()" "yes" 0 sh -c 'a=$("$1" +%s); b=$(python3 -c "import time; print(int(time.time()))"); [ $((b - a)) -ge 0 ] && [ $((b - a)) -le 2 ] && echo yes' sh "$U"
# the flags and widths of POSIX.1-2024 (for the year now, four digits)
y=$(TZ=UTC0 "$U" +%Y) c=$(TZ=UTC0 "$U" +%C) g=$(TZ=UTC0 "$U" +%G) md=$(TZ=UTC0 "$U" +%m-%d)
t "%+4Y: no sign" "$y" 0 env TZ=UTC0 "$U" +%+4Y
t "%04Y" "$y" 0 env TZ=UTC0 "$U" +%04Y
t "%+6Y: a sign, then zeros" "+0$y" 0 env TZ=UTC0 "$U" +%+6Y
t "%06Y: zeros" "00$y" 0 env TZ=UTC0 "$U" +%06Y
t "%6Y: zeros" "00$y" 0 env TZ=UTC0 "$U" +%6Y
t "%+2C: no sign" "$c" 0 env TZ=UTC0 "$U" +%+2C
t "%+3C: a sign" "+$c" 0 env TZ=UTC0 "$U" +%+3C
t "%04C" "00$c" 0 env TZ=UTC0 "$U" +%04C
t "%+10F: ISO 8601" "$y-$md" 0 env TZ=UTC0 "$U" +%+10F
t "%+12F: a sign" "+0$y-$md" 0 env TZ=UTC0 "$U" +%+12F
t "%012F" "00$y-$md" 0 env TZ=UTC0 "$U" +%012F
t "%+4F: as width 6" "$y-$md" 0 env TZ=UTC0 "$U" +%+4F
t "%+6G" "+0$g" 0 env TZ=UTC0 "$U" +%+6G
t "%08G" "0000$g" 0 env TZ=UTC0 "$U" +%08G
t "flags among other text" "<$y> <+0$y> %" 0 env TZ=UTC0 "$U" '+<%Y> <%+6Y> %%'
# formats
t "an empty format: an empty line" "" 0 "$U" +
t "an empty format: one newline" "1" 0 sh -c '"$1" + | wc -l | tr -d " "' sh "$U"
t "no conversions" "hello world" 0 "$U" '+hello world'
t "%%" "%" 0 "$U" +%%
t "%n and %t" "a${nl}b	c" 0 "$U" '+a%nb%tc'
t "a % at the end: itself" "x%" 0 "$U" '+x%'
t "a long result" "40000" 0 sh -c 'f=$(awk "BEGIN { while (i++ < 10000) printf \"%%Y\" }"); "$1" "+$f" | tr -d "\n" | wc -c | tr -d " "' sh "$U"
t "LC_TIME: the names of the C locale" "yes" 0 sh -c 'LC_ALL=C "$1" +%A | grep -E "^(Sun|Mon|Tues|Wednes|Thurs|Fri|Satur)day\$" >/dev/null && echo yes' sh "$U"
# setting the date: only with a value that is not valid as root (it would
# set the clock); without privileges, a valid one fails
for d in 1301 13010000 00010000 01320000 01012400 01010060 0230000024 0431000024 010100002x +x1 0101000000000 ""; do
    case $d in +*) continue;; esac
    t "not a valid date: [$d]" "" 1 "$U" "$d"
done
t "not a valid date: with -u" "" 1 "$U" -u 13010000
t "not a valid date: nothing on the standard output" "" 0 sh -c '"$1" 13010000 2>/dev/null | wc -c | tr -d " " | grep -q "^0\$"' sh "$U"
t "not a valid date: a diagnostic" "" 0 sh -c '"$1" 13010000 2>&1 >/dev/null | grep -q .' sh "$U"
if ! $root; then
    t "without privileges: the clock is not set" "" 1 "$U" 0101000099
    t "without privileges: with cc" "" 1 "$U" 010100002030
    t "without privileges: -u" "" 1 "$U" -u 01010000
fi
# usage
t "two operands" "" 2 "$U" +%Y +%m
t "an unknown option" "" 2 "$U" -x
t "--" "$("$U" -u +%Y)" 0 "$U" -u -- +%Y
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: date +%Y -u: -u is a second operand" "" 2 "$U" +%Y -u
[ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" > /dev/full' sh "$U"

done_tests date
