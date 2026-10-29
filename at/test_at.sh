#!/bin/sh
# test_at.sh - tests for at (POSIX.1-2024).  Run: sh test_at.sh
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
U=${U:-$PWD/at}
T=${TMPDIR:-/tmp}/test_at.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# a spool and a configuration directory of the tests' own (at, batch and
# atd take AT_SPOOL and AT_CONFDIR when they are not set-user-ID); mailx
# is a script that keeps what it is sent; the times in UTC
D=${U%/*}
B=$D/batch A=$D/atd
mkdir spool conf bin
export AT_SPOOL="$T/spool" AT_CONFDIR="$T/conf" TZ=UTC0
unset SHELL                     # the jobs' interpreter: sh
printf '#!/bin/sh\n{ printf "TO:"; printf " %%s" "$@"; echo; cat; } >> "%s/mail"\n' "$T" > bin/mailx
chmod +x bin/mailx
$root || : > conf/at.deny       # everyone may use at
atd() { PATH="$T/bin:$PATH" "$A" -s 2>/dev/null; }
clean() { rm -f spool/[0-9]* spool/=* mail; }
# the time at writes for a job, as seconds since the Epoch
when() { "$U" -l "$1" | cut -f 2 | python3 -c 'import sys, time, calendar; print(calendar.timegm(time.strptime(sys.stdin.read().strip(), "%a %b %d %H:%M:%S %Y")))'; }
# exp SPEC: what the time should be, computed by Python from the time now
cat > exp.py <<'PY'
import calendar, sys, time
spec = sys.argv[1]
now = int(time.time())
t = time.gmtime(now)
def at(y, mo, d, h, mi):
    return calendar.timegm((y, mo, d, h, mi, 0, 0, 0, 0))
today = (t.tm_year, t.tm_mon, t.tm_mday)
if spec == "now":
    print(now)
elif spec == "now + 1 hour":
    print(now + 3600)
elif spec == "now next minute":
    print(now + 60)
elif spec == "noon today":
    print(at(*today, 12, 0))
elif spec == "11:59pm tomorrow":
    print(at(*today, 23, 59) + 86400)
elif spec == "0930":
    x = at(*today, 9, 30)
    print(x if (t.tm_hour, t.tm_min) <= (9, 30) else x + 86400)
elif spec == "noon sunday":
    ahead = (6 - t.tm_wday) % 7       # Python: Monday 0, Sunday 6
    x = at(*today, 12, 0) + ahead * 86400
    print(x if ahead or (t.tm_hour, t.tm_min) <= (12, 0) else x + 7 * 86400)
PY
exp() { python3 exp.py "$1"; }
# close SPEC: the job's time is what Python finds, within 2 seconds
close() {
    _e=$(exp "$1"); _id=$(echo true | "$U" $1 2>&1 | sed -n 's/^job \([^ ]*\) at .*/\1/p')
    [ -n "$_id" ] || return 1
    _w=$(when "$_id"); [ $((_w - _e)) -le 2 ] && [ $((_e - _w)) -le 2 ]
}

# submitting
t "the job ID and the time on the standard error" "yes" 0 sh -c 'echo true | "$1" -t 203001011200 2>&1 >/dev/null | grep -E "^job [0-9]+\.a at Tue Jan  1 12:00:00 2030\$" >/dev/null && echo yes' sh "$U"
t "nothing on the standard output" "" 0 sh -c 'echo true | "$1" -t 203001011200 2>/dev/null' sh "$U"
t "the job IDs differ" "yes" 0 sh -c 'a=$(echo true | "$1" now 2>&1); b=$(echo true | "$1" now 2>&1); [ "$a" != "$b" ] && echo yes' sh "$U"
clean
# the times: absolute ones
tm() { _n=$1 _e=$2; shift 2; t "$_n" "$_e" 0 sh -c 'echo true | "$0" "$@" 2>&1 | sed "s/^job [^ ]* at //"' "$U" "$@"; }
tm "-t CCYYMMDDhhmm" "Tue Jan  1 12:00:00 2030" -t 203001011200
tm "-t YYMMDDhhmm" "Tue Jan  1 12:00:00 2030" -t 3001011200
tm "-t with seconds" "Tue Jan  1 12:00:30 2030" -t 203001011200.30
tm "noon, a month, a day and a year" "Tue Jan  1 12:00:00 2030" noon Jan 1, 2030
tm "midnight" "Tue Dec 31 00:00:00 2030" midnight Dec 31, 2030
tm "hh:mm, a full month name" "Wed Jan  2 16:30:00 2030" 16:30 January 2, 2030
tm "hhmm" "Wed Jan  2 16:30:00 2030" 1630 Jan 2, 2030
tm "an hour" "Wed Jan  2 09:00:00 2030" 9 Jan 2, 2030
tm "pm" "Wed Jan  2 16:30:00 2030" 4:30pm Jan 2, 2030
tm "12am" "Wed Jan  2 00:00:00 2030" 12am Jan 2, 2030
tm "12pm" "Wed Jan  2 12:00:00 2030" 12pm Jan 2, 2030
tm "am and pm apart, any case" "Wed Jan  2 04:00:00 2030" 4 AM JAN 2, 2030
tm "the operands joined" "Wed Jan  2 16:30:00 2030" "4:30pm Jan" "2," 2030
tm "February 29 of a leap year" "Sun Feb 29 10:00:00 2032" 10:00 Feb 29, 2032
tm "+ days" "Thu Jan  3 12:00:00 2030" noon Jan 1, 2030 + 2 days
tm "+ minutes" "Tue Jan  1 13:30:00 2030" noon Jan 1, 2030 + 90 minutes
tm "+ hours" "Wed Jan  2 00:00:00 2030" noon Jan 1, 2030 + 12 hours
tm "+ weeks" "Tue Jan 22 12:00:00 2030" noon Jan 1, 2030 + 3 weeks
tm "+ months" "Mon Apr  1 12:00:00 2030" noon Jan 1, 2030 + 3 months
tm "+ year (singular)" "Wed Jan  1 12:00:00 2031" noon Jan 1, 2030 + 1 year
tm "next week" "Tue Jan  8 12:00:00 2030" noon Jan 1, 2030 next week
tm "next month" "Fri Feb  1 12:00:00 2030" noon Jan 1, 2030 next month
t "utc: a time zone" "Tue Jan  1 07:00:00 2030" 0 sh -c 'echo true | TZ=EST5 "$1" noon utc Jan 1, 2030 2>&1 | sed "s/^job [^ ]* at //"' sh "$U"
t "the time zone of TZ" "Tue Jan  1 12:00:00 2030" 0 sh -c 'echo true | TZ=EST5 "$1" noon Jan 1, 2030 2>&1 | sed "s/^job [^ ]* at //"' sh "$U"
t "TZ: the job's time is in it" "1893517200" 0 sh -c 'id=$(echo true | TZ=EST5 "$1" noon Jan 1, 2030 2>&1 | cut -d " " -f 2); sed -n "s/^# time //p" "$2/$id"' sh "$U" "$T/spool"
clean
# the times: relative ones
ok "now" close now
ok "now + 1 hour" close "now + 1 hour"
ok "now next minute" close "now next minute"
ok "noon today" close "noon today"
ok "11:59pm tomorrow" close "11:59pm tomorrow"
ok "0930: today or tomorrow" close 0930
ok "noon sunday: the next Sunday" close "noon sunday"
clean
# times that are not valid
for s in 25:00 12:60 13pm 0pm 123 12345 "noon Feb 30, 2030" "noon Jan 1, 30" "noon + 1 fortnight" "now + x hours" "noon nosuchday" "x" "noon Jan" "now 5"; do
    t "not a time: $s" "" 1 sh -c 'echo true | "$1" $2' sh "$U" "$s"
done
t "-t: not a time" "" 1 sh -c 'echo true | "$1" -t 2030010112' sh "$U"
t "-t: a month 13" "" 1 sh -c 'echo true | "$1" -t 203013011200' sh "$U"
t "-t: not digits" "" 1 sh -c 'echo true | "$1" -t 20300101120x' sh "$U"
# the queue, listing, removing
clean
id1=$(echo true | "$U" -t 203001011200 2>&1 | cut -d " " -f 2)
id2=$(echo true | "$U" -t 202901011200 2>&1 | cut -d " " -f 2)
id3=$(echo true | "$U" -q c -t 203101011200 2>&1 | cut -d " " -f 2)
t "-l: by time, ID tab date" "$id2	Mon Jan  1 12:00:00 2029${nl}$id1	Tue Jan  1 12:00:00 2030${nl}$id3	Wed Jan  1 12:00:00 2031" 0 "$U" -l
t "-l with job IDs" "$id1	Tue Jan  1 12:00:00 2030" 0 "$U" -l "$id1"
t "-l -q: a queue" "$id3	Wed Jan  1 12:00:00 2031" 0 "$U" -l -q c
t "-q: the queue is in the job ID" "c" 0 sh -c 'echo "$1" | sed "s/.*\.//"' sh "$id3"
t "-l: a job that is not there" "" 1 "$U" -l 999999.a
t "-r" "$id2	Mon Jan  1 12:00:00 2029${nl}$id3	Wed Jan  1 12:00:00 2031" 0 sh -c '"$1" -r "$2" && "$1" -l' sh "$U" "$id1"
t "-r several" "" 0 sh -c '"$1" -r "$2" "$3" && "$1" -l' sh "$U" "$id2" "$id3"
t "-r a job that is not there" "" 1 "$U" -r 999999.a
t "-r not a job ID" "" 1 "$U" -r ../x
# batch
clean
t "batch: queue b, now" "yes" 0 sh -c 'echo true | "$1" 2>&1 | grep -E "^job [0-9]+\.b at " >/dev/null && echo yes' sh "$B"
t "batch: mail on" "1" 0 sh -c 'sed -n "s/^# mail //p" "$1"/*.b' sh "$T/spool"
t "batch: no operands" "" 2 sh -c 'echo true | "$1" now' sh "$B"
t "batch: no options" "" 2 sh -c 'echo true | "$1" -m' sh "$B"
clean
# running the jobs: atd -s
mkdir work
t "a job: the environment, the directory, the mask" "foo=a b|$T/work|0027" 0 sh -c 'cd work && umask 027 && FOO="a b" "$1" now 2>/dev/null <<"X"
echo "foo=$FOO|$(pwd)|$(umask)" > ../out
X
PATH="$2/bin:$PATH" "$3" -s 2>/dev/null; cat ../out' sh "$U" "$T" "$A"
t "a job: a value with quotes" "it's \"x\"" 0 sh -c 'BAR="it'"'"'s \"x\"" "$1" now 2>/dev/null <<"X"
printf "%s\n" "$BAR" > out
X
PATH="$2/bin:$PATH" "$3" -s 2>/dev/null; cat out' sh "$U" "$T" "$A"
t "a job: its output is mailed" "TO: -s Output from your job JOB $(id -un)${nl}to the output" 0 sh -c 'id=$(echo "echo to the output" | "$1" now 2>&1 | cut -d " " -f 2); PATH="$2/bin:$PATH" "$3" -s 2>/dev/null; sed "s/job $id/job JOB/" mail' sh "$U" "$T" "$A"
clean
t "a job without output: no mail" "" 1 sh -c 'echo true | "$1" now 2>/dev/null; PATH="$2/bin:$PATH" "$3" -s 2>/dev/null; test -s mail' sh "$U" "$T" "$A"
t "-m: mail without output" "yes" 0 sh -c 'echo true | "$1" -m now 2>/dev/null; PATH="$2/bin:$PATH" "$3" -s 2>/dev/null; grep "^TO: -s Output from your job" mail >/dev/null && echo yes' sh "$U" "$T" "$A"
clean
t "the job is done and gone" "" 0 sh -c 'echo true | "$1" now 2>/dev/null; PATH="$2/bin:$PATH" "$3" -s 2>/dev/null; "$1" -l' sh "$U" "$T" "$A"
t "a job not yet due is not run" "yes" 0 sh -c 'echo ": > ran" | "$1" -t 203001011200 2>/dev/null; PATH="$2/bin:$PATH" "$3" -s 2>/dev/null; [ ! -e ran ] && "$1" -l | grep 2030 >/dev/null && echo yes' sh "$U" "$T" "$A"
clean
t "a job: a session of its own, no terminal" "True${nl}not a tty" 0 sh -c 'echo "python3 -c \"import os; print(os.getsid(0) == os.getppid())\" > sid; tty < /dev/tty >> sid 2>/dev/null || echo not a tty >> sid" | "$1" now 2>/dev/null; PATH="$2/bin:$PATH" "$3" -s 2>/dev/null; cat sid' sh "$U" "$T" "$A"
t "-f file" "from the file" 0 sh -c 'echo "echo from the file > ff" > jobf; "$1" -f jobf now 2>/dev/null < /dev/null; PATH="$2/bin:$PATH" "$3" -s 2>/dev/null; cat ff' sh "$U" "$T" "$A"
t "-f a missing file" "" 1 "$U" -f no_such_file now
t "SHELL: the job's interpreter" "yes" 0 sh -c 'printf "#!/bin/sh\necho by-myshell > $PWD/myshell.ran\nexec sh\n" > myshell; chmod +x myshell; echo true | SHELL=$PWD/myshell "$1" now 2>/dev/null; PATH="$2/bin:$PATH" "$3" -s 2>/dev/null; [ -f myshell.ran ] && echo yes' sh "$U" "$T" "$A"
clean
t "batch jobs: one after the other" "1 start${nl}1 end${nl}2 start${nl}2 end" 0 sh -c 'echo "echo 1 start >> seq; sleep 1; echo 1 end >> seq" | "$1" 2>/dev/null; echo "echo 2 start >> seq; echo 2 end >> seq" | "$1" 2>/dev/null; PATH="$2/bin:$PATH" "$3" -s 2>/dev/null; cat seq' sh "$B" "$T" "$A"
clean
# at.allow and at.deny
if $root; then
    t "root may use at whatever the files" "" 0 sh -c 'echo root > conf/at.deny; echo true | "$1" -t 203001011200 2>/dev/null; r=$?; rm -f conf/at.deny; exit $r' sh "$U"
else
    me=$(id -un)
    t "neither file: not allowed" "" 1 sh -c 'rm -f conf/at.deny conf/at.allow; echo true | "$1" now 2>/dev/null; r=$?; : > conf/at.deny; exit $r' sh "$U"
    t "an empty at.deny: allowed" "" 0 sh -c 'echo true | "$1" -t 203001011200 2>/dev/null' sh "$U"
    t "at.deny lists the user" "" 1 sh -c 'echo "$2" > conf/at.deny; echo true | "$1" now 2>/dev/null; r=$?; : > conf/at.deny; exit $r' sh "$U" "$me"
    t "at.allow lists the user" "" 0 sh -c 'echo "$2" > conf/at.allow; echo true | "$1" -t 203001011200 2>/dev/null; r=$?; rm conf/at.allow; exit $r' sh "$U" "$me"
    t "at.allow without the user" "" 1 sh -c 'echo nobody_else > conf/at.allow; echo true | "$1" now 2>/dev/null; r=$?; rm conf/at.allow; exit $r' sh "$U"
    t "at.allow without the user: -l too" "" 1 sh -c 'echo nobody_else > conf/at.allow; "$1" -l 2>/dev/null; r=$?; rm conf/at.allow; exit $r' sh "$U"
    t "batch: at.allow without the user" "" 1 sh -c 'echo nobody_else > conf/at.allow; echo true | "$1" 2>/dev/null; r=$?; rm conf/at.allow; exit $r' sh "$B"
fi
clean
# usage
t "no timespec" "" 2 sh -c 'echo true | "$1"' sh "$U"
t "-r without job IDs" "" 2 "$U" -r
t "-r and -l" "" 2 "$U" -r -l 1.a
t "-t and a timespec" "" 2 sh -c 'echo true | "$1" -t 203001011200 now' sh "$U"
t "-q not a letter" "" 2 sh -c 'echo true | "$1" -q 1 now' sh "$U"
t "-q upper case" "" 2 sh -c 'echo true | "$1" -q B now' sh "$U"
t "-l -q and job IDs" "" 2 "$U" -l -q a 1.a
t "an unknown option" "" 2 "$U" -x now
t "--" "yes" 0 sh -c 'echo true | "$1" -- now 2>&1 | grep "^job" >/dev/null && echo yes' sh "$U"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: at now -m: -m is part of the timespec" "" 1 sh -c 'echo true | "$1" now -m' sh "$U"
clean

done_tests at
