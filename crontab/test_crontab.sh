#!/bin/sh
# test_crontab.sh - tests for crontab (POSIX.1-2024).  Run: sh test_crontab.sh
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
U=${U:-$PWD/crontab}
T=${TMPDIR:-/tmp}/test_crontab.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# a spool and a configuration directory of the tests' own (crontab and
# crond take CRON_SPOOL and CRON_CONFDIR when they are not set-user-ID);
# mailx is a script that keeps what it is sent; crond -s -T runs the
# entries of one minute
D=${U%/*}
C=$D/crond
mkdir spool conf bin
export CRON_SPOOL="$T/spool" CRON_CONFDIR="$T/conf" TZ=UTC0
printf '#!/bin/sh\n{ printf "TO:"; printf " %%s" "$@"; echo; cat; } >> "%s/mail"\n' "$T" > bin/mailx
chmod +x bin/mailx
$root || : > conf/cron.deny     # everyone may use crontab
me=$(id -un)
# at TIME: run crond for that minute; then the mail, without the TO: lines
cat > at <<X
#!/bin/sh
rm -f "$T/mail"; PATH="$T/bin:\$PATH" "$C" -s -T "\$1" 2>/dev/null
[ -f "$T/mail" ] && grep -v '^TO:' "$T/mail"
exit 0
X
chmod +x at
at() { "$T/at" "$@"; }
# installing, listing, removing
printf '# a comment\n\n  # an indented one\n* * * * * echo x\n' > tab
t "install from a file, then -l" "$(cat tab)" 0 sh -c '"$1" tab && "$1" -l' sh "$U"
t "install from the standard input" "0 0 * * * date" 0 sh -c 'echo "0 0 * * * date" | "$1" && "$1" -l' sh "$U"
t "replaces the entry" "1 1 * * * a" 0 sh -c 'echo "1 1 * * * a" | "$1" && "$1" -l' sh "$U"
t "an empty entry" "" 0 sh -c '"$1" < /dev/null && "$1" -l' sh "$U"
t "a last line without a newline: one is added" "1 2 3 4 5 x" 0 sh -c 'printf "1 2 3 4 5 x" | "$1" && "$1" -l' sh "$U"
t "-r" "" 0 "$U" -r
t "-l without an entry" "" 1 "$U" -l
t "-r without an entry" "" 1 "$U" -r
# patterns: the valid ones
for p in "* * * * *" "0 0 1 1 0" "59 23 31 12 6" "0-59 0-23 1-31 1-12 0-6" "1,2,3 4-5,7 1,15 1-3,6 0,6" "00 01 01 01 00" "5 * * * 1-5"; do
    t "a valid entry: $p" "" 0 sh -c 'echo "$2 cmd" | "$1"' sh "$U" "$p"
done
# the ones that are not: reported with the line number; nothing installed
echo "0 0 * * * kept" | "$U"
for p in "60 * * * *" "* 24 * * *" "* * 0 * *" "* * 32 * *" "* * * 0 *" "* * * 13 *" "* * * * 7" \
         "5-1 * * * *" "1, * * * *" ",1 * * * *" "1- * * * *" "a * * * *" "*/5 * * * *" "-1 * * * *" "1--2 * * * *" \
         "* * * *"; do
    t "not valid: $p" "" 1 sh -c 'echo "$2 cmd" | "$1"' sh "$U" "$p"
done
t "not valid: no command" "" 1 sh -c 'echo "* * * * *" | "$1"' sh "$U"
t "not valid: the line number" "yes" 0 sh -c 'printf "# ok\n0 0 * * * a\n99 * * * * b\n" | "$1" 2>&1 | grep "line 3" >/dev/null && echo yes' sh "$U"
t "not valid: the entry before is kept" "0 0 * * * kept" 0 "$U" -l
t "a NUL byte" "" 1 sh -c 'printf "0 0 * * * a\0b\n" | "$1"' sh "$U"
t "a missing file" "" 1 "$U" no_such_file
# -e
printf '#!/bin/sh\necho "5 5 * * * edited" >> "$1"\n' > edit1
printf '#!/bin/sh\nexit 0\n' > edit0
printf '#!/bin/sh\nexit 3\n' > editfail
printf '#!/bin/sh\necho "99 * * * * bad" >> "$1"\n' > editbad
chmod +x edit1 edit0 editfail editbad
t "-e: the editor's changes installed" "0 0 * * * kept${nl}5 5 * * * edited" 0 sh -c 'EDITOR=./edit1 "$1" -e && "$1" -l' sh "$U"
t "-e: unchanged" "" 0 sh -c 'EDITOR=./edit0 "$1" -e' sh "$U"
t "-e: unchanged: a note" "yes" 0 sh -c 'EDITOR=./edit0 "$1" -e 2>&1 | grep -i "no changes" >/dev/null && echo yes' sh "$U"
t "-e: the editor fails: nothing installed" "0 0 * * * kept${nl}5 5 * * * edited" 1 sh -c 'EDITOR=./editfail "$1" -e 2>/dev/null; r=$?; "$1" -l; exit $r' sh "$U"
t "-e: not valid: nothing installed" "0 0 * * * kept${nl}5 5 * * * edited" 1 sh -c 'EDITOR=./editbad "$1" -e 2>/dev/null; r=$?; "$1" -l; exit $r' sh "$U"
t "-e: EDITOR with arguments" "0 0 * * * x" 0 sh -c '"$1" -r; EDITOR="sh -c '"'"'echo \"0 0 * * * x\" > \"\$1\"'"'"' sh" "$1" -e && "$1" -l' sh "$U"
t "-e: no entry: an empty one to edit" "5 5 * * * edited" 0 sh -c '"$1" -r; EDITOR=./edit1 "$1" -e && "$1" -l' sh "$U"
# crond: the times
cat > tab <<'X'
30 12 * * * echo A minute and hour
0 0 1 1 * echo B January 1
0 0 * * 1 echo C Mondays
0 0 15 * 3 echo D the 15th or Wednesdays
0 0 * 2 3 echo E February or Wednesdays
0-5,10 1 * * * echo F a list
X
"$U" tab
t "minute and hour" "A minute and hour" 0 at 203001041230
t "minute and hour: not another minute" "" 0 at 203001041231
t "day of the month and month, any weekday" "B January 1" 0 at 203001010000
t "a weekday, any day of the month" "C Mondays" 0 at 203001070000
t "day of the month or weekday: the day" "D the 15th or Wednesdays" 0 at 203003150000
t "day of the month or weekday: the weekday" "D the 15th or Wednesdays${nl}E February or Wednesdays" 0 sh -c '"$1/at" 203003130000 | sort' sh "$T"
t "a month and a weekday: the month's days" "E February or Wednesdays" 0 at 203002050000
t "nothing at another time" "" 0 at 203003140000
t "a list: in it" "F a list" 0 at 203001040103
t "a list: in it, after the range" "F a list" 0 at 203001040110
t "a list: not in it" "" 0 at 203001040107
# crond: the commands
echo '* * * * * echo "$HOME|$LOGNAME|$SHELL|$PATH|$(pwd)"' | "$U"
home=$(python3 -c 'import os, pwd; print(pwd.getpwuid(os.getuid()).pw_dir)')
cwd=$home; [ -d "$home" ] || cwd=/
stdsh=$(getconf PATH | tr ':' '\n' | while read d; do [ -x "$d/sh" ] && { echo "$d/sh"; break; }; done)
t "the environment (the sh of getconf PATH) and the directory (HOME, else /)" "$home|$me|$stdsh|$(getconf PATH)|$cwd" 0 at 203001010000
t "the mail: to the user" "TO: -s Cron <$me> echo X $me" 0 sh -c 'echo "* * * * * echo X" | "$1"; rm -f mail; PATH="$3/bin:$PATH" "$2" -s -T 203001010000; grep "^TO:" mail' sh "$U" "$C" "$T"
t "no output: no mail" "" 1 sh -c 'echo "* * * * * true" | "$1"; rm -f mail; PATH="$3/bin:$PATH" "$2" -s -T 203001010000; test -e mail' sh "$U" "$C" "$T"
t "the standard error too" "err" 0 sh -c 'echo "* * * * * echo err >&2" | "$1"; "$2/at" 203001010000' sh "$U" "$T"
t "% begins the standard input, each one a newline" "one${nl}two" 0 sh -c 'echo "* * * * * cat%one%two" | "$1"; "$2/at" 203001010000' sh "$U" "$T"
t "\\% is a %" "100%" 0 sh -c 'echo "* * * * * echo 100\\%" | "$1"; "$2/at" 203001010000' sh "$U" "$T"
t "other backslashes are the shell's" "a b" 0 sh -c 'echo "* * * * * echo a\\ b" | "$1"; "$2/at" 203001010000' sh "$U" "$T"
t "a session of its own" "True" 0 sh -c 'echo "* * * * * python3 -c \"import os; print(os.getsid(0) == os.getppid())\"" | "$1"; "$2/at" 203001010000' sh "$U" "$T"
t "several entries" "1${nl}2" 0 sh -c 'printf "* * * * * echo 1\n* * * * * sleep 1; echo 2\n" | "$1"; "$2/at" 203001010000 | sort' sh "$U" "$T"
t "a removed entry is not run" "" 0 sh -c 'echo "* * * * * echo X" | "$1"; "$1" -r; "$2/at" 203001010000' sh "$U" "$T"
# cron.allow and cron.deny
if $root; then
    t "root may use crontab whatever the files" "" 0 sh -c 'echo root > conf/cron.deny; "$1" -l > /dev/null 2>&1; echo "0 0 * * * x" | "$1"; r=$?; rm -f conf/cron.deny; exit $r' sh "$U"
else
    t "neither file: not allowed" "" 1 sh -c 'rm -f conf/cron.deny conf/cron.allow; echo "0 0 * * * x" | "$1" 2>/dev/null; r=$?; : > conf/cron.deny; exit $r' sh "$U"
    t "an empty cron.deny: allowed" "" 0 sh -c 'echo "0 0 * * * x" | "$1"' sh "$U"
    t "cron.deny lists the user" "" 1 sh -c 'echo "$2" > conf/cron.deny; "$1" -l 2>/dev/null; r=$?; : > conf/cron.deny; exit $r' sh "$U" "$me"
    t "cron.allow lists the user" "" 0 sh -c 'echo "$2" > conf/cron.allow; echo "0 0 * * * x" | "$1"; r=$?; rm conf/cron.allow; exit $r' sh "$U" "$me"
    t "cron.allow without the user" "" 1 sh -c 'echo nobody_else > conf/cron.allow; "$1" -r 2>/dev/null; r=$?; rm conf/cron.allow; exit $r' sh "$U"
fi
# usage
t "-e and -l" "" 2 "$U" -e -l
t "-l and a file" "" 2 "$U" -l tab
t "two files" "" 2 "$U" tab tab
t "an unknown option" "" 2 "$U" -x
t "--" "0 0 * * * dd" 0 sh -c 'echo "0 0 * * * dd" > -l; "$1" -- -l && "$1" -l' sh "$U"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: crontab tab -l: -l is a second file" "" 2 "$U" tab -l

done_tests crontab
