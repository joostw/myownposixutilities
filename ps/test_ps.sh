#!/bin/sh
# test_ps.sh - tests for ps (POSIX.1-2024).  Run: sh test_ps.sh
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
U=${U:-$PWD/ps}
T=${TMPDIR:-/tmp}/test_ps.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
# ptyrun FDS COMMAND...: COMMAND with the descriptors FDS on a terminal (../testlib/ptyrun.py)
TL=$(cd ../testlib && pwd) || exit 2; export TL
ptyrun() { python3 "$TL/ptyrun.py" "$@"; }
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# processes to look at: sleeps, each the leader of a new session (no
# terminal), started by Python; and the system's ps (procps), with the
# options POSIX gives it, to compare
pids=
spawn() { python3 -c 'import os, sys; os.setsid(); os.execvp(sys.argv[1], sys.argv[1:])' "$@" >/dev/null 2>&1 & pids="$pids $!"; last=$!; }
trap 'kill $pids 2>/dev/null; chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
spawn sleep 60; s1=$last
spawn nice -n 5 sleep 61; s2=$last
long=$(awk 'BEGIN { while (i++ < 3000) printf "x" }')
spawn sh -c 'sleep 62; :' sh "$long"; s3=$last
# a zombie: its parent does not wait for it
spawn python3 -c 'import os, time; os.fork() == 0 and os._exit(0); time.sleep(60)'; zp=$last
sleep 1
z=$(ps -A -o pid= -o ppid= | awk -v p=$zp '$2 == p { print $1 }')
me=$(id -un) mg=$(id -gn)
o() { "$U" -p "$1" -o "$2=" | sed 's/^ *//; s/ *$//'; }
t "-o pid" "$s1" 0 o $s1 pid
t "-o ppid" "$$" 0 o $s1 ppid
t "-o pgid: a session leader leads its group" "$s1" 0 o $s1 pgid
t "-o user" "$me" 0 o $s1 user
t "-o ruser" "$me" 0 o $s1 ruser
t "-o group" "$mg" 0 o $s1 group
t "-o rgroup" "$mg" 0 o $s1 rgroup
t "-o nice" "0" 0 o $s1 nice
t "-o nice: nice -n 5" "5" 0 o $s2 nice
t "-o comm" "sleep" 0 o $s1 comm
t "-o args" "sleep 60" 0 o $s1 args
t "-o tty: none" "?" 0 o $s1 tty
t "-o time" "00:00:00" 0 o $s1 time
t "-o etime: [[dd-]hh:]mm:ss" "yes" 0 sh -c 'e=$("$1" -p $2 -o etime=); case $e in *[0-9][0-9]:[0-9][0-9]) echo yes;; *) echo "$e";; esac' sh "$U" $s1
t "-o vsz: kilobytes" "yes" 0 sh -c 'v=$("$1" -p $2 -o vsz=); [ "$v" -gt 0 ] && echo yes' sh "$U" $s1
t "-o pcpu: a number" "yes" 0 sh -c '"$1" -p $2 -o pcpu= | grep -E "^ *[0-9]+(\.[0-9])?\$" >/dev/null && echo yes' sh "$U" $s1
t "a zombie: <defunct>" "[python3] <defunct>" 0 o "$z" args
t "a zombie: comm, unmarked" "python3" 0 o "$z" comm
t "a zombie: CMD of the default listing" "python3 <defunct>" 0 sh -c '"$1" -p $2 | sed 1d | sed "s/^.*:[0-9][0-9] //"' sh "$U" "$z"
t "-l: a zombie's state" "Z" 0 sh -c '"$1" -l -p $2 | awk "NR == 2 { print \$2 }"' sh "$U" "$z"
# as the system's ps finds them
for f in pid ppid pgid user ruser group rgroup nice comm tty time vsz; do
    t "-o $f, as the system's ps" "$(ps -p "$s1,$s2,$s3,$zp" -o "$f=" | sed 's/^ *//; s/ *$//')" 0 sh -c '"$1" -p "$2" -o "$3=" | sed "s/^ *//; s/ *\$//"' sh "$U" "$s1,$s2,$s3,$zp" $f
done
t "-o args, as the system's ps" "$(ps -ww -p "$s1,$s2" -o args=)" 0 "$U" -ww -p "$s1,$s2" -o args=
# headers
t "the default header, right-aligned over the values" "PID yes" 0 sh -c '"$1" -p $2 -o pid > o; h=$(sed 1q o); v=$(sed -n 2p o); [ ${#h} -eq ${#v} ] && echo $h yes' sh "$U" $s1
t "the headers of the standard" "RUSER USER RGROUP GROUP PID PPID PGID %CPU VSZ NI ELAPSED TIME TT COMMAND COMMAND" 0 sh -c '"$1" -p $2 -o ruser,user,rgroup,group,pid,ppid,pgid,pcpu,vsz,nice,etime,time,tty,comm,args | sed 1q | tr -s " " | sed "s/^ //"' sh "$U" $s1
t "a header of one's own" "THE PID" 0 sh -c '"$1" -p $2 -o "pid=THE PID" | sed 1q | sed "s/^ *//"' sh "$U" $s1
t "a header: the rest of the argument" "X,user" 0 sh -c '"$1" -p $2 -o "pid=X,user" | sed 1q' sh "$U" $s1
t "a header: the rest of the argument, the field alone" "1" 0 sh -c '"$1" -p $2 -o "pid=X,user" | sed 1d | wc -w | tr -d " "' sh "$U" $s1
t "all headers empty: no header line" "1" 0 sh -c '"$1" -p $2 -o pid= -o user= | wc -l | tr -d " "' sh "$U" $s1
t "an empty header: the default's width" "yes" 0 sh -c 'l=$("$1" -p $2 -o user= -o pid=); case $l in "$3"*) n=${l#"$3"}; [ ${#3} -lt 4 ] && case $n in "    "*) ;; *) exit 1;; esac; echo yes;; esac' sh "$U" $s1 "$me"
t "several -o: one list" "PID USER" 0 sh -c '"$1" -p $2 -o pid -o user | sed 1q | tr -s " " | sed "s/^ //"' sh "$U" $s1
t "-o with blanks" "PID PPID" 0 sh -c '"$1" -p $2 -o "pid ppid" | sed 1q | tr -s " " | sed "s/^ //"' sh "$U" $s1
t "columns: as wide as the header" "yes" 0 sh -c '"$1" -p $2 -o pid,ppid | awk "NR == 1 { h = index(\$0, \"PPID\") + 3 } NR == 2 { print (length(\$0) == h ? \"yes\" : \$0) }"' sh "$U" $s1
t "a field more than once" "$s1 $s1" 0 sh -c 'echo $("$1" -p $2 -o pid= -o pid=)' sh "$U" $s1
t "name=: the rest of the argument is the header" ",user=" 0 sh -c '"$1" -p $2 -o pid=,user= | sed 1q | sed "s/^ *//"' sh "$U" $s1
# selection
t "-p: a list with commas" "$s1 $s2" 0 sh -c 'echo $("$1" -p $2,$3 -o pid=)' sh "$U" $s1 $s2
t "-p: a list with blanks" "$s1 $s2" 0 sh -c 'echo $("$1" -p "$2 $3" -o pid=)' sh "$U" $s1 $s2
t "-p: by process ID" "$s1 $s2" 0 sh -c 'echo $("$1" -p $3,$2 -o pid=)' sh "$U" $s1 $s2
t "-A: process 1" "1" 0 sh -c '"$1" -A -o pid= | sed "s/ //g" | grep -x 1' sh "$U"
t "-e: as -A" "same" 0 sh -c '"$1" -e -o pid= > a; "$1" -A -o pid= > b; [ $(wc -l < a) -ge 3 ] && [ $(($(wc -l < a) - $(wc -l < b))) -le 2 ] && [ $(($(wc -l < b) - $(wc -l < a))) -le 2 ] && echo same' sh "$U"
t "-d: no session leaders" "" 1 sh -c '"$1" -d -o pid= | sed "s/ //g" | grep -x $2' sh "$U" $s1
t "-a: none without a terminal" "" 1 sh -c '"$1" -a -o pid= | sed "s/ //g" | grep -x $2' sh "$U" $s1
t "-g: by session" "$s1" 0 sh -c '"$1" -g $2 -o pid= | sed "s/ //g"' sh "$U" $s1
t "-G: by real group" "yes" 0 sh -c '"$1" -G $(id -gr) -o pid= | sed "s/ //g" | grep -x $2 >/dev/null && echo yes' sh "$U" $s1
t "-G: a group name" "yes" 0 sh -c '"$1" -G $(id -gn) -o pid= | sed "s/ //g" | grep -x $2 >/dev/null && echo yes' sh "$U" $s1
t "-u: a login name" "yes" 0 sh -c '"$1" -u $(id -un) -o pid= | sed "s/ //g" | grep -x $2 >/dev/null && echo yes' sh "$U" $s1
t "-u: a user ID" "yes" 0 sh -c '"$1" -u $(id -u) -o pid= | sed "s/ //g" | grep -x $2 >/dev/null && echo yes' sh "$U" $s1
t "-U: the real user" "yes" 0 sh -c '"$1" -U $(id -ru) -o pid= | sed "s/ //g" | grep -x $2 >/dev/null && echo yes' sh "$U" $s1
t "-u: another user: not ours" "" 1 sh -c '"$1" -u 54321 -o pid= | grep .' sh "$U"
t "several options: either" "$s1 $s2" 0 sh -c 'echo $("$1" -p $2 -g $3 -o pid=)' sh "$U" $s1 $s2
t "-n: ignored" "$s1" 0 sh -c '"$1" -n /nonexistent -p $2 -o pid= | sed "s/ //g"' sh "$U" $s1
# terminals: on a pseudo-terminal of its own
t "the default: this terminal's processes of this user" "yes" 0 sh -c 'python3 "$TL/ptyrun.py" 012 sh -c "\"\$1\" > out; tty > tty" sh "$1"; t=$(sed "s,^/dev/,," tty); awk -v t=$t "NR > 1 && \$2 != t { bad = 1 } END { print (bad || NR < 3) ? \"no\" : \"yes\" }" out' sh "$U"
t "the default: no other terminal" "" 1 sh -c 'python3 "$TL/ptyrun.py" 012 "$1" | grep -w "$2"' sh "$U" $s1
t "-t: a terminal by name" "yes" 0 sh -c 'python3 "$TL/ptyrun.py" 012 sh -c "\"\$1\" -t \$(tty | sed s,^/dev/,,) -o tty= -o comm= > out" sh "$1"; grep "pts/[0-9]* *ps\$" out >/dev/null && echo yes' sh "$U"
t "-t: a /dev/ name" "yes" 0 sh -c 'python3 "$TL/ptyrun.py" 012 sh -c "\"\$1\" -t \$(tty) -o comm= > out" sh "$1"; grep -x ps out >/dev/null && echo yes' sh "$U"
t "-t ?: no terminal" "yes" 0 sh -c '"$1" -t "?" -o pid= | sed "s/ //g" | grep -x $2 >/dev/null && echo yes' sh "$U" $s1
t "-o tty on a terminal: as the system's ps" "same" 0 sh -c 'python3 "$TL/ptyrun.py" 012 sh -c "\"\$1\" -p \$\$ -o tty= > a; ps -p \$\$ -o tty= > b" sh "$1"; [ "$(sed "s/ //g" a)" = "$(sed "s/ //g" b)" ] && echo same' sh "$U"
# the listings
t "the default listing" "PID TTY TIME CMD" 0 sh -c '"$1" -p $2 | sed 1q | tr -s " " | sed "s/^ //"' sh "$U" $s1
t "the default listing: the command name" "$s1 ? 00:00:00 sleep" 0 sh -c '"$1" -p $2 | sed 1d | tr -s " " | sed "s/^ //"' sh "$U" $s1
t "-f" "UID PID PPID C STIME TTY TIME CMD" 0 sh -c '"$1" -f -p $2 | sed 1q | tr -s " " | sed "s/^ //"' sh "$U" $s1
t "-f: the login name and the arguments" "$me $s1 $$ sleep 60" 0 sh -c '"$1" -f -p $2 | sed 1d | awk "{ print \$1, \$2, \$3, \$8, \$9 }"' sh "$U" $s1 $$
t "-l" "F S UID PID PPID C PRI NI ADDR SZ WCHAN TTY TIME CMD" 0 sh -c '"$1" -l -p $2 | sed 1q | tr -s " " | sed "s/^ //"' sh "$U" $s1
t "-l: the user ID number" "$(id -u)" 0 sh -c '"$1" -l -p $2 | sed 1d | awk "{ print \$3 }"' sh "$U" $s1
t "-l: NI" "5" 0 sh -c '"$1" -l -p $2 | sed 1d | awk "{ print \$8 }"' sh "$U" $s2
t "-lf" "F S UID PID PPID C PRI NI ADDR SZ WCHAN STIME TTY TIME CMD" 0 sh -c '"$1" -lf -p $2 | sed 1q | tr -s " " | sed "s/^ //"' sh "$U" $s1
t "-f: STIME is HH:MM today" "yes" 0 sh -c '"$1" -f -p $2 | sed 1d | awk "{ print \$5 }" | grep -E "^[0-2][0-9]:[0-5][0-9]\$" >/dev/null && echo yes' sh "$U" $s1
t "the -l names in -o" "S PRI" 0 sh -c '"$1" -p $2 -o s,pri | sed 1q | tr -s " " | sed "s/^ //"' sh "$U" $s1
# long lines
t "lines cut at {LINE_MAX}" "2048" 0 sh -c 'unset COLUMNS; "$1" -p $2 -o args= | awk "{ print length(\$0) }"' sh "$U" $s3
t "-w: as COLUMNS of 132 at least" "2048" 0 sh -c 'COLUMNS=80 "$1" -w -p $2 -o args= | awk "{ print length(\$0) }"' sh "$U" $s3
t "COLUMNS larger than {LINE_MAX}" "2500" 0 sh -c 'COLUMNS=2500 "$1" -p $2 -o args= | awk "{ print length(\$0) }"' sh "$U" $s3
t "-ww: not cut" "3021" 0 sh -c '"$1" -ww -p $2 -o args= | awk "{ print length(\$0) }"' sh "$U" $s3
# errors
t "an unknown format name" "" 1 "$U" -o pid,nosuch
t "a process ID that is not a number" "" 1 "$U" -p abc
t "an empty list" "" 1 "$U" -p ","
t "an unknown option" "" 1 "$U" -x
t "an operand" "" 1 "$U" x
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: ps x -A: x is an operand" "" 1 "$U" x -A
[ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" -A > /dev/full' sh "$U"

done_tests ps
