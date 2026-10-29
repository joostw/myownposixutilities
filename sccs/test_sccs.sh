#!/bin/sh
# test_sccs.sh - tests for sccs (POSIX.1-2024).  Run: sh test_sccs.sh
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
U=${U:-$PWD/sccs}
T=${TMPDIR:-/tmp}/test_sccs.$$
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
# the programs: one, acting by its name (the links made by make)
D=${U%/*}
for p in admin delta get prs rmdel sact unget val what; do
    [ -x "$D/$p" ] || { echo "test_sccs: $D/$p is missing (make first)"; exit 2; }
done
export D U nl
who=$(id -un); export who
# sed "$ND": dates and times as the SCCS files have them become DATE
ND='s|[0-9][0-9]/[0-9][0-9]/[0-9][0-9] [0-9][0-9]:[0-9][0-9]:[0-9][0-9]|DATE|g'
export ND
# in_new: cd to a new directory of its own
N=0
in_new() { N=$((N + 1)); mkdir "$T/w$N" && cd "$T/w$N"; }
# s NAME EXPECTED STATUS SCRIPT: SCRIPT (sh) in a new directory, standard
# error dropped; e: the same with standard error kept
s() { in_new; t "$1" "$2" "$3" sh -c "$4"; }
e() { in_new; t "$1" "$2" "$3" sh -c "exec 2>&1; $4"; }
# a file with an SCCS file made from it: s.f with "%M% %I%", a, b, c
mk='printf "%%M%% %%I%%\na\nb\nc\n" > f && "$D/admin" -if -yfirst s.f 2>/dev/null && rm f'
export mk
# ed1: edit 1.1 into 1.2 (b changed, d added)
ed1='"$D/get" -e s.f >/dev/null && printf "%%M%% %%I%%\na\nB\nc\nd\n" > f && "$D/delta" -ysecond s.f >/dev/null'
export ed1

# ---- admin ----
s "admin -i: the SCCS file" "-r--r--r-- yes" 0 "$mk; ls -l s.f | cut -c 1-10 | tr '\n' ' '; [ -f s.f ] && echo yes"
s "admin -i: the first line: SOH h and a checksum of five digits" "yes" 0 "$mk; od -An -c s.f | sed 1q | grep -q '001   h   [0-9]   [0-9]   [0-9]   [0-9]   [0-9]' && echo yes"
s "admin -i: the text of delta 1.1" "f 1.1${nl}a${nl}b${nl}c" 0 "$mk; \"\$D/get\" -p s.f 2>/dev/null"
s "admin -i: no x-file or z-file left" "s.f" 0 "$mk; ls"
s "admin -i: the comment" "first" 0 "$mk; \"\$D/prs\" -d:C: s.f"
s "admin -i: a multi-line comment" "one${nl}two" 0 "printf 'x\n' > f; \"\$D/admin\" -if -y'one
two' s.f 2>/dev/null; \"\$D/prs\" -d:C: s.f"
s "admin: the default comment" "date and time created DATE by $who" 0 "printf 'x\n' > f; \"\$D/admin\" -if s.f 2>/dev/null; \"\$D/prs\" -d:C: s.f | sed \"\$ND\""
s "admin -i without a name: the standard input" "x${nl}y" 0 "printf 'x\ny\n' | \"\$D/admin\" -i s.f 2>/dev/null; \"\$D/get\" -p -s s.f 2>/dev/null"
s "admin -n: no text" "1.1 00000" 0 "\"\$D/admin\" -n s.f; \"\$D/prs\" -d':I: :Li:' s.f"
s "admin -n: several files" "s.a s.b" 0 "\"\$D/admin\" -n s.a s.b; echo s.*"
s "admin -r: the first release" "2.1" 0 "printf 'x\n' > f; \"\$D/admin\" -if -r2 s.f 2>/dev/null; \"\$D/prs\" -d:I: s.f"
s "admin -r: release and level" "3.4" 0 "printf 'x\n' > f; \"\$D/admin\" -if -r3.4 s.f 2>/dev/null; \"\$D/prs\" -d:I: s.f"
s "admin -r: a branch SID is an error" "" 1 "printf 'x\n' > f; \"\$D/admin\" -if -r1.1.1 s.f 2>/dev/null"
s "admin -i: the file exists already" "" 1 "$mk; printf 'x\n' > g; \"\$D/admin\" -ig s.f 2>/dev/null"
s "admin: a name without s." "" 1 "printf 'x\n' > f; \"\$D/admin\" -if f.s 2>/dev/null"
e "admin: no id keywords: a warning" "admin: warning: s.f: No id keywords." 0 "printf 'x\n' > f; \"\$D/admin\" -if s.f"
s "admin -fi: no id keywords is an error" "no" 1 "printf 'x\n' > f; \"\$D/admin\" -if -fi s.f 2>/dev/null; s=\$?; [ -e s.f ] && echo yes || echo no; exit \$s"
s "admin -t: the descriptive text" "about${nl}the file" 0 "printf 'about\nthe file\n' > d; $mk; \"\$D/admin\" -td s.f; \"\$D/prs\" -d:FD: s.f"
s "admin -t without a name: removed" "none" 0 "printf 'about\n' > d; $mk; \"\$D/admin\" -td s.f; \"\$D/admin\" -t s.f; \"\$D/prs\" -d:FD: s.f"
s "admin -f: the flags" "branch${nl}ceiling	50${nl}default SID	1.1${nl}floor	2${nl}joint edit${nl}locked releases	3 4${nl}module	mod${nl}null delta${nl}csect name	qq${nl}type	typ" 0 "$mk; \"\$D/admin\" -fb -fc50 -fd1.1 -ff2 -fj -fl3,4 -fmmod -fn -fqqq -fttyp s.f; \"\$D/prs\" -d:FL: s.f"
s "admin -d: flags removed" "branch" 0 "$mk; \"\$D/admin\" -fb -fj -fttyp s.f; \"\$D/admin\" -dj -dt s.f; \"\$D/prs\" -d:FL: s.f"
s "admin -fl and -dl: lists of releases" "1 3${nl}a${nl}none" 0 "$mk; \"\$D/admin\" -fl1,2,3 s.f; \"\$D/admin\" -dl2 s.f; \"\$D/prs\" -d:LK: s.f; \"\$D/admin\" -fla s.f; \"\$D/prs\" -d:LK: s.f; \"\$D/admin\" -dla s.f; \"\$D/prs\" -d:LK: s.f"
s "admin -f with a bad flag" "" 1 "$mk; \"\$D/admin\" -fx s.f 2>/dev/null"
s "admin -fc: not a number" "" 1 "$mk; \"\$D/admin\" -fcx s.f 2>/dev/null"
s "admin -a and -e: the users" "ann${nl}!bob${nl}${nl}none" 0 "$mk; \"\$D/admin\" -aann -a!bob s.f; \"\$D/prs\" -d:UN: s.f; \"\$D/admin\" -eann -e!bob s.f; \"\$D/prs\" -d:UN: s.f"
s "admin -m: MRs need the v flag" "" 1 "printf 'x\n' > f; \"\$D/admin\" -if -m12 s.f 2>/dev/null"
s "admin -fv -m: the MRs" "12${nl}34" 0 "printf 'x\n' > f; \"\$D/admin\" -if -fv -m'12 34' s.f 2>/dev/null; \"\$D/prs\" -d:MR: s.f"
s "admin -fv without -m: an error" "" 1 "printf 'x\n' > f; \"\$D/admin\" -if -fv s.f 2>/dev/null"
s "admin -fv: the validation program says no" "" 1 "printf 'x\n' > f; \"\$D/admin\" -if -fvfalse -m12 s.f 2>/dev/null"
s "admin -h: a good file" "" 0 "$mk; \"\$D/admin\" -h s.f"
s "admin -h: a changed file" "" 1 "$mk; chmod u+w s.f; sed 's/^a\$/A/' s.f > t && cat t > s.f; \"\$D/admin\" -h s.f 2>/dev/null"
s "admin -z: the checksum again" "A" 0 "$mk; chmod u+w s.f; sed 's/^a\$/A/' s.f > t && cat t > s.f; \"\$D/admin\" -z s.f && \"\$D/admin\" -h s.f && \"\$D/get\" -p s.f 2>/dev/null | sed -n 2p"
s "admin: a changed file is not used" "" 1 "$mk; chmod u+w s.f; sed 's/^a\$/A/' s.f > t && cat t > s.f; \"\$D/get\" -p s.f 2>/dev/null"
s "admin: the mode of an existing file is kept" "-r--r-----" 0 "$mk; chmod 440 s.f; \"\$D/admin\" -fb s.f; ls -l s.f | cut -c 1-10"
s "admin: a directory: its s. files" "branch${nl}${nl}branch" 0 "mkdir d; cd d; $mk; cp s.f s.g; printf x > other; cd ..; \"\$D/admin\" -fb d; \"\$D/prs\" -d:FL: d/s.f d/s.g"
s "admin -: the names from the standard input" "branch${nl}${nl}branch" 0 "$mk; cp s.f s.g; printf 's.f\ns.g\nnot-sccs\n' | \"\$D/admin\" -fb -; \"\$D/prs\" -d:FL: s.f s.g"
s "admin: a lock (z-file) in the way" "" 1 "$mk; : > z.f; \"\$D/admin\" -fb s.f 2>/dev/null"
s "admin -h with -f: a usage error" "" 2 "$mk; \"\$D/admin\" -h -fb s.f 2>/dev/null"
s "admin -i with two files: a usage error" "" 2 "printf 'x\n' > f; \"\$D/admin\" -if s.a s.b 2>/dev/null"
s "admin: no operands" "" 2 "\"\$D/admin\" -n 2>/dev/null"

# ---- get ----
s "get: the g-file, read-only, the keywords expanded" "-r--r--r-- f 1.1" 0 "$mk; \"\$D/get\" s.f >/dev/null 2>&1; ls -l f | cut -c 1-10 | tr '\n' ' '; sed 1q f"
s "get: the SID and the lines" "1.1${nl}4 lines" 0 "$mk; \"\$D/get\" s.f"
s "get -s: nothing on standard output" "" 0 "$mk; \"\$D/get\" -s s.f"
s "get -p: the text on standard output" "f 1.1${nl}a${nl}b${nl}c" 0 "$mk; \"\$D/get\" -p s.f 2>/dev/null; [ ! -e f ]"
e "get -p: the SID on standard error" "1.1${nl}4 lines" 0 "$mk; \"\$D/get\" -p s.f 2>&1 >/dev/null"
s "get -k: no expansion, writable" "-rw-r--r-- %M% %I%" 0 "$mk; umask 022; \"\$D/get\" -k s.f >/dev/null 2>&1; ls -l f | cut -c 1-10 | tr '\n' ' '; sed 1q f"
s "get -m: the SID before each line" "1.1	f 1.2${nl}1.1	a${nl}1.2	B" 0 "$mk; $ed1; \"\$D/get\" -p -m s.f 2>/dev/null | sed 3q"
s "get -n: the module name before each line" "f	f 1.2${nl}f	a" 0 "$mk; $ed1; \"\$D/get\" -p -n s.f 2>/dev/null | sed 2q"
s "get -m -n: both" "f	1.1	a" 0 "$mk; \"\$D/get\" -p -m -n s.f 2>/dev/null | sed -n 2p"
s "get: a writable g-file in the way" "" 1 "$mk; echo mine > f; \"\$D/get\" s.f 2>/dev/null"
s "get: a read-only g-file is replaced" "f 1.1" 0 "$mk; echo old > f; chmod 444 f; \"\$D/get\" -s s.f && sed 1q f"
s "get -g: no g-file" "1.1 no" 0 "$mk; \"\$D/get\" -g s.f | tr '\n' ' '; [ -e f ] && echo yes || echo no"
s "get -r: an older version" "f 1.1${nl}a${nl}b${nl}c" 0 "$mk; $ed1; \"\$D/get\" -p -r1.1 s.f 2>/dev/null"
s "get -r R: the highest level of the release" "1.2" 0 "$mk; $ed1; \"\$D/get\" -g -r1 s.f"
s "get -r: a higher release than any: the top" "1.2" 0 "$mk; $ed1; \"\$D/get\" -g -r5 s.f"
s "get -r: a SID that does not exist" "" 1 "$mk; \"\$D/get\" -g -r1.5 s.f 2>/dev/null"
s "get -r: not a SID" "" 1 "$mk; \"\$D/get\" -g -r1.0 s.f 2>/dev/null"
s "get: the d flag is the default SID" "1.1" 0 "$mk; $ed1; \"\$D/admin\" -fd1.1 s.f; \"\$D/get\" -g s.f"
s "get -i: a delta included" "Included:${nl}1.2${nl}1.1" 0 "$mk; $ed1; \"\$D/get\" -g -r1.1 -i1.2 s.f"
s "get -x: a delta excluded" "Excluded:${nl}1.2${nl}1.2${nl}4 lines" 0 "$mk; $ed1; \"\$D/get\" -p -x1.2 s.f 2>&1 >/dev/null"
s "get -x: the text without it" "a${nl}b${nl}c" 0 "$mk; $ed1; \"\$D/get\" -p -s -x1.2 s.f 2>/dev/null | sed 1d"
s "get -i: a range" "Included:${nl}1.3${nl}1.2" 0 "$mk; $ed1; \"\$D/get\" -e s.f >/dev/null; echo e >> f; \"\$D/delta\" -y s.f >/dev/null; \"\$D/get\" -g -r1.1 -i1.2-1.3 s.f | sed 3q"
s "get -c: a cutoff before every delta: no lines" "1.1${nl}0 lines" 0 "$mk; \"\$D/get\" -c690101 s.f 2>/dev/null"
s "get -c: 68 is 2068, after the deltas" "1.1${nl}4 lines" 0 "$mk; \"\$D/get\" -p -c681231 s.f 2>&1 >/dev/null"
s "get -c: with separators" "4 lines" 0 "$mk; \"\$D/get\" -p -c'68/12/31 23:59:59' s.f 2>&1 >/dev/null | sed -n 2p"
s "get -c: not a date" "" 1 "$mk; \"\$D/get\" -g -c99x13 s.f 2>/dev/null"
s "get -l: the l-file" "-r--r--r--${nl}    1.2	DATE $who${nl}	second${nl}${nl}    1.1	DATE $who${nl}	first" 0 "$mk; $ed1; \"\$D/get\" -s -g -l s.f; ls -l l.f | cut -c 1-10; sed \"\$ND\" l.f"
s "get -l: excluded deltas marked" "**X 1.2	DATE $who" 0 "$mk; $ed1; \"\$D/get\" -s -g -l -x1.2 s.f; sed \"\$ND\" l.f | sed 1q"
s "get -L: the summary on standard output" "    1.1	DATE $who${nl}	first" 0 "$mk; \"\$D/get\" -s -g -L s.f | sed \"\$ND\""
s "get: the keywords" "f 1.1 1 1 0 0 typ qq s.f @(#) 1${nl}@(#)f	1.1${nl}@(#)typ f 1.1@(#)" 0 "printf '%%M%% %%I%% %%R%% %%L%% %%B%% %%S%% %%Y%% %%Q%% %%F%% %%Z%% %%C%%\\n%%W%%\\n%%A%%\\n' > f; \"\$D/admin\" -if -fttyp -fqqq s.f; rm f; \"\$D/get\" -p s.f 2>/dev/null"
s "get: the keywords of a branch delta" "1.1.1.1 1 1 1 1" 0 "printf '%%I%% %%R%% %%L%% %%B%% %%S%%\\n' > f; \"\$D/admin\" -if s.f; rm f; \"\$D/get\" -e -s s.f; echo x >> f; \"\$D/delta\" -s -y s.f; \"\$D/get\" -e -s -r1.1 s.f; echo y >> f; \"\$D/delta\" -s -y s.f; \"\$D/get\" -p -r1.1.1.1 s.f 2>/dev/null | sed 1q"
s "get: %M% %I% %C%, %D% %H% %T% %E% %G% %U% as dates" "yes" 0 "printf '%%D%% %%H%% %%T%%\n%%E%% %%G%% %%U%%\nline %%C%%\n' > f; \"\$D/admin\" -if s.f; rm f; \"\$D/get\" -p s.f 2>/dev/null > out; grep -q '^[0-9][0-9]/[0-9][0-9]/[0-9][0-9] [0-9][0-9]/[0-9][0-9]/[0-9][0-9] [0-9][0-9]:[0-9][0-9]:[0-9][0-9]\$' out && sed -n 2p out | grep -q '^[0-9][0-9]/[0-9][0-9]/[0-9][0-9] [0-9][0-9]/[0-9][0-9]/[0-9][0-9] [0-9][0-9]:[0-9][0-9]:[0-9][0-9]\$' && grep -q '^line 3\$' out && echo yes"
s "get: %P% is the absolute path" "yes" 0 "printf '%%P%%\n' > f; \"\$D/admin\" -if s.f; rm f; [ \"\$(\"\$D/get\" -p s.f 2>/dev/null)\" = \"\$PWD/s.f\" ] && echo yes"
e "get: no id keywords: a warning" "get: warning: s.f: No id keywords.${nl}1.1${nl}1 lines" 0 "printf 'x\n' > f; \"\$D/admin\" -if s.f 2>/dev/null; rm f; \"\$D/get\" s.f"
s "get: the i flag makes it an error" "no" 1 "printf 'x\n' > f; \"\$D/admin\" -if s.f 2>/dev/null; \"\$D/admin\" -fi s.f; rm f; \"\$D/get\" s.f 2>/dev/null; s=\$?; [ -e f ] && echo yes || echo no; exit \$s"
s "get: several files: the names" "${nl}s.a:${nl}1.1${nl}4 lines${nl}${nl}s.b:${nl}1.1${nl}4 lines" 0 "$mk; mv s.f s.a; $mk; mv s.f s.b; \"\$D/get\" -p s.a s.b 2>&1 >/dev/null"
s "get: a directory" "a b" 0 "mkdir d; cd d; $mk; mv s.f s.a; $mk; mv s.f s.b; cd ..; \"\$D/get\" -s d; ls -d a b | tr '\n' ' ' | sed 's/ \$//'"
s "get -: names from the standard input" "a" 0 "$mk; mv s.f s.a; printf 's.a\n' | \"\$D/get\" -s -; ls a"
s "get: a missing file, the others done" "f" 1 "$mk; \"\$D/get\" -s s.none s.f 2>/dev/null; s=\$?; ls f; exit \$s"
s "get: no operands" "" 2 "\"\$D/get\" 2>/dev/null"

# get -e and the p-file
s "get -e: the new delta" "1.1${nl}new delta 1.2${nl}4 lines" 0 "$mk; \"\$D/get\" -e s.f"
s "get -e: the p-file" "1.1 1.2 $who DATE" 0 "$mk; \"\$D/get\" -e -s s.f; sed \"\$ND\" p.f"
s "get -e: the g-file is writable, keywords not expanded" "-rw------- %M% %I%" 0 "$mk; umask 077; \"\$D/get\" -e -s s.f; ls -l f | cut -c 1-10 | tr '\n' ' '; sed 1q f"
s "get -e twice: being edited" "" 1 "$mk; \"\$D/get\" -e -s s.f; rm f; \"\$D/get\" -e -s s.f 2>/dev/null"
s "get -e twice with the j flag: a branch" "1.1${nl}new delta 1.1.1.1${nl}4 lines" 0 "$mk; \"\$D/admin\" -fj s.f; \"\$D/get\" -e -s s.f; rm f; \"\$D/get\" -e s.f"
s "get -e -r of an old delta: a branch" "1.1${nl}new delta 1.1.1.1" 0 "$mk; $ed1; \"\$D/get\" -e -r1.1 -g s.f"
s "get -e -b without the b flag: no branch" "new delta 1.3" 0 "$mk; $ed1; \"\$D/get\" -e -b -g s.f | sed -n 2p"
s "get -e -b with the b flag: a branch" "new delta 1.2.1.1" 0 "$mk; $ed1; \"\$D/admin\" -fb s.f; \"\$D/get\" -e -b -g s.f | sed -n 2p"
s "get -e -r R: a new release" "1.2${nl}new delta 2.1" 0 "$mk; $ed1; \"\$D/get\" -e -r2 -g s.f"
s "get -e -r of a branch: the next on it" "1.1.1.1${nl}new delta 1.1.1.2" 0 "$mk; $ed1; \"\$D/get\" -e -r1.1 -s s.f; \"\$D/delta\" -y s.f >/dev/null 2>&1; \"\$D/get\" -e -r1.1.1 -g s.f"
s "get -e -i: recorded in the p-file" "1.1 1.2 $who DATE -i1.1" 0 "$mk; \"\$D/get\" -e -s -i1.1 s.f; sed \"\$ND\" p.f"
s "get -e -g: the p-file only" "no yes" 0 "$mk; \"\$D/get\" -e -g -s s.f; { [ -e f ] && echo yes || echo no; } | tr '\n' ' '; [ -e p.f ] && echo yes"
s "get -e: the ceiling" "" 1 "$mk; \"\$D/admin\" -fc0001 s.f 2>/dev/null; \"\$D/admin\" -fc1 s.f; $ed1; \"\$D/get\" -e -r2 -g s.f >/dev/null 2>&1 && \"\$D/delta\" -y s.f >/dev/null 2>&1; \"\$D/get\" -e -g -r2.1 s.f 2>/dev/null"
s "get -e: the floor" "" 1 "$mk; \"\$D/admin\" -ff2 s.f; \"\$D/get\" -e -g s.f 2>/dev/null"
s "get -e: a locked release" "" 1 "$mk; \"\$D/admin\" -fl1 s.f; \"\$D/get\" -e -g s.f 2>/dev/null"
s "get -e: all releases locked" "" 1 "$mk; \"\$D/admin\" -fla s.f; \"\$D/get\" -e -g s.f 2>/dev/null"
s "get -e: a user list without me" "" 1 "$mk; \"\$D/admin\" -anotme s.f; \"\$D/get\" -e -g s.f 2>/dev/null"
s "get -e: a user list with me" "new delta 1.2" 0 "$mk; \"\$D/admin\" -anotme -a\"\$who\" s.f; \"\$D/get\" -e -g s.f | sed -n 2p"
s "get -e: me denied" "" 1 "$mk; \"\$D/admin\" -a\"\$who\" -a\"!\$who\" s.f; \"\$D/get\" -e -g s.f 2>/dev/null"
s "get -e: my group" "new delta 1.2" 0 "$mk; \"\$D/admin\" -a\$(id -g) s.f; \"\$D/get\" -e -g s.f | sed -n 2p"
s "get -e: a lock (z-file) in the way" "" 1 "$mk; : > z.f; \"\$D/get\" -e -g s.f 2>/dev/null"

# ---- delta ----
s "delta: the report" "1.2${nl}2 inserted${nl}1 deleted${nl}3 unchanged" 0 "$mk; \"\$D/get\" -e -s s.f; printf '%%M%% %%I%%\na\nB\nc\nd\n' > f; \"\$D/delta\" -yok s.f"
s "delta: the new version" "f 1.2${nl}a${nl}B${nl}c${nl}d" 0 "$mk; $ed1; \"\$D/get\" -p s.f 2>/dev/null"
s "delta: the old version is still there" "f 1.1${nl}a${nl}b${nl}c" 0 "$mk; $ed1; \"\$D/get\" -p -r1.1 s.f 2>/dev/null"
s "delta: the g-file and the p-file go" "s.f" 0 "$mk; $ed1; ls"
s "delta -n: the g-file stays" "f s.f" 0 "$mk; \"\$D/get\" -e -s s.f; echo x >> f; \"\$D/delta\" -n -y s.f >/dev/null; echo * "
s "delta -s: no report" "" 0 "$mk; \"\$D/get\" -e -s s.f; echo x >> f; \"\$D/delta\" -s -y s.f"
s "delta: the statistics in the file" "00002/00001/00003" 0 "$mk; $ed1; \"\$D/prs\" -d:DL: s.f"
s "delta -y: the comment, more lines" "one${nl}two" 0 "$mk; \"\$D/get\" -e -s s.f; \"\$D/delta\" -s -y'one
two' s.f; \"\$D/prs\" -d:C: s.f"
s "delta: the comment from the standard input" "from input${nl}and more" 0 "$mk; \"\$D/get\" -e -s s.f; printf 'from input\\\\\nand more\nnot this\n' | \"\$D/delta\" -s s.f; \"\$D/prs\" -d:C: s.f"
s "delta: on a terminal, the prompt" "yes" 0 "$mk; \"\$D/get\" -e -s s.f; PTYRUN_INPUT=\"typed\$nl\" python3 \"\$TL/ptyrun.py\" 01 \"\$D/delta\" -s s.f > out; grep -q 'comments? ' out && \"\$D/prs\" -d:C: s.f | grep -q '^typed\$' && echo yes"
s "delta -p: the differences" "3c3${nl}< b${nl}---${nl}> B${nl}4a5${nl}> d" 0 "$mk; \"\$D/get\" -e -s s.f; printf '%%M%% %%I%%\na\nB\nc\nd\n' > f; \"\$D/delta\" -s -p -y s.f"
s "delta: no edit" "" 1 "$mk; \"\$D/get\" -s s.f; \"\$D/delta\" -y s.f 2>/dev/null"
s "delta: no g-file" "" 1 "$mk; \"\$D/get\" -e -s s.f; rm f; \"\$D/delta\" -y s.f 2>/dev/null"
s "delta -r: one of two edits" "1.1.1.1" 1 "$mk; \"\$D/admin\" -fj s.f; \"\$D/get\" -e -s s.f; rm f; \"\$D/get\" -e -s s.f; \"\$D/delta\" -y s.f 2>/dev/null; s=\$?; \"\$D/delta\" -s -r1.1.1.1 -y s.f; \"\$D/prs\" -d:I: s.f; exit \$s" 
s "delta: two edits need -r" "" 1 "$mk; \"\$D/admin\" -fj s.f; \"\$D/get\" -e -s s.f; rm f; \"\$D/get\" -e -s s.f; \"\$D/delta\" -y s.f 2>/dev/null"
s "delta -m: not without the v flag" "" 1 "$mk; \"\$D/get\" -e -s s.f; \"\$D/delta\" -m12 -y s.f 2>/dev/null"
s "delta: the v flag: MRs" "12${nl}34" 0 "$mk; \"\$D/admin\" -fv s.f; \"\$D/get\" -e -s s.f; \"\$D/delta\" -s -m'12 34' -y s.f; \"\$D/prs\" -d:MR: s.f"
s "delta: the v flag: MRs from the standard input" "56" 0 "$mk; \"\$D/admin\" -fv s.f; \"\$D/get\" -e -s s.f; printf '56\ncomment\n' | \"\$D/delta\" -s s.f; \"\$D/prs\" -d:MR: s.f"
s "delta: the v flag and no MRs" "" 1 "$mk; \"\$D/admin\" -fv s.f; \"\$D/get\" -e -s s.f; \"\$D/delta\" -y s.f 2>/dev/null </dev/null"
s "delta: an MR validation program says no" "yes" 1 "$mk; printf '#!/bin/sh\nexit 1\n' > check; chmod +x check; \"\$D/admin\" -fv\$PWD/check s.f; \"\$D/get\" -e -s s.f; \"\$D/delta\" -m1 -y s.f 2>/dev/null; s=\$?; [ -e p.f ] && echo yes; exit \$s"
s "delta: the MR program gets module, type, MRs" "f typ 7 8" 0 "$mk; printf '#!/bin/sh\necho \"\$*\" > args\n' > check; chmod +x check; \"\$D/admin\" -fv\$PWD/check -fttyp s.f; \"\$D/get\" -e -s s.f; \"\$D/delta\" -s -m'7 8' -y s.f; cat args"
s "delta: the n flag: null deltas for skipped releases" "4.1 3.1 2.1 1.1" 0 "$mk; \"\$D/admin\" -fn s.f; \"\$D/get\" -e -s -r4 s.f; \"\$D/delta\" -s -y s.f; \"\$D/prs\" -d:I: -e -r4.1 s.f | tr '\n' ' ' | sed 's/ \$//'"
s "delta: a null delta's comment" "AUTO NULL DELTA" 0 "$mk; \"\$D/admin\" -fn s.f; \"\$D/get\" -e -s -r3 s.f; \"\$D/delta\" -s -y s.f; \"\$D/prs\" -d:C: -r2.1 s.f"
s "delta: -i and -x of get -e are recorded" "2   / 2//" 0 "$mk; $ed1; \"\$D/get\" -e -s -r1.1 -i1.2 s.f; \"\$D/delta\" -s -y s.f; \"\$D/prs\" -d':Dn: :Dx: :Dg: / :DI:' s.f"
s "delta -g: deltas ignored" "1" 0 "$mk; $ed1; \"\$D/get\" -e -s s.f; \"\$D/delta\" -s -g1.1 -y s.f; \"\$D/prs\" -d:Dg: s.f"
s "delta -: -y is needed" "" 2 "$mk; echo s.f | \"\$D/delta\" - 2>/dev/null"
s "delta: a line starting with SOH" "" 1 "$mk; \"\$D/get\" -e -s s.f; printf '\001x\n' > f; \"\$D/delta\" -y s.f 2>/dev/null"
s "delta: a branch, then the trunk unchanged" "f 1.2${nl}a${nl}B" 0 "$mk; $ed1; \"\$D/get\" -e -s -r1.1 s.f; echo branch >> f; \"\$D/delta\" -s -y s.f; \"\$D/get\" -p s.f 2>/dev/null | sed 3q"
s "delta: the branch version" "f 1.1.1.1${nl}a${nl}b${nl}c${nl}branch" 0 "$mk; $ed1; \"\$D/get\" -e -s -r1.1 s.f; echo branch >> f; \"\$D/delta\" -s -y s.f; \"\$D/get\" -p -r1.1.1.1 s.f 2>/dev/null"
s "delta: many versions, each one back" "ok" 0 "$mk; i=1; while [ \$i -le 12 ]; do \"\$D/get\" -e -s s.f; awk -v i=\$i 'NR % 3 != i % 3 { print } { if (NR == i) print \"line\", i }' f > g; mv g f; cp f v.\$i; \"\$D/delta\" -s -y s.f; i=\$((i + 1)); done; i=1; while [ \$i -le 12 ]; do \"\$D/get\" -p -k -s -r1.\$((i + 1)) s.f > out; cmp -s out v.\$i || exit 1; i=\$((i + 1)); done; echo ok"

# ---- prs ----
s "prs: the default" "s.f:${nl}${nl}D 1.2 DATE $who 2 1	00002/00001/00003${nl}MRs:${nl}COMMENTS:${nl}second${nl}${nl}D 1.1 DATE $who 1 0	00004/00000/00000${nl}MRs:${nl}COMMENTS:${nl}first" 0 "$mk; $ed1; \"\$D/prs\" s.f | sed \"\$ND\""
s "prs -r: one delta, no name" "D 1.1 DATE $who 1 0	00004/00000/00000${nl}MRs:${nl}COMMENTS:${nl}first" 0 "$mk; $ed1; \"\$D/prs\" -r1.1 s.f | sed \"\$ND\""
s "prs -d: one line for the newest" "1.2" 0 "$mk; $ed1; \"\$D/prs\" -d:I: s.f"
s "prs -d -r: the delta" "1.1 1 1" 0 "$mk; $ed1; \"\$D/prs\" -d':I: :R: :L:' -r1.1 s.f"
s "prs -r without a SID: the newest" "1.2" 0 "$mk; $ed1; \"\$D/prs\" -d:I: -r s.f"
s "prs -e: earlier ones" "1.2${nl}1.1" 0 "$mk; $ed1; \"\$D/prs\" -d:I: -e -r1.2 s.f"
s "prs -l: later ones" "1.2" 0 "$mk; $ed1; \"\$D/prs\" -d:I: -l -r1.2 s.f"
s "prs -c -e: before a date" "" 0 "$mk; \"\$D/prs\" -d:I: -e -c690101 s.f"
s "prs -c -l: after a date" "1.1" 0 "$mk; \"\$D/prs\" -d:I: -l -c690101 s.f"
s "prs: text, \\t and \\n" "a	b${nl}c 1.1" 0 "$mk; \"\$D/prs\" -d'a\tb\nc :I:' s.f"
s "prs: :Dt: :DL: :Li: :Ld: :Lu:" "D 1.2 DATE $who 2 1 00002/00001/00003 00002 00001 00003" 0 "$mk; $ed1; \"\$D/prs\" -d':Dt: :DL: :Li: :Ld: :Lu:' s.f | sed \"\$ND\""
s "prs: :DT: :I: :R: :L: :B: :S:" "D 1.1.1.1 1 1 1 1" 0 "$mk; $ed1; \"\$D/get\" -e -r1.1 -s s.f; \"\$D/delta\" -s -y s.f; \"\$D/prs\" -d':DT: :I: :R: :L: :B: :S:' -r1.1.1.1 s.f"
s "prs: :D: :T: and their parts" "yes" 0 "$mk; \"\$D/prs\" -d':D: :T: :Dy:/:Dm:/:Dd: :Th:::Tm:::Ts:' s.f > o; read d t d2 t2 < o; [ \"\$d\" = \"\$d2\" ] && [ \"\$t\" = \"\$t2\" ] && echo yes"
s "prs: :P: :DS: :DP:" "$who 2 1" 0 "$mk; $ed1; \"\$D/prs\" -d':P: :DS: :DP:' s.f"
s "prs: :MR: :C:" "c1${nl}first" 0 "$mk; \"\$D/prs\" -d':MR:c1\n:C:' s.f"
s "prs: :FL: none and :UN: none" "none" 0 "$mk; \"\$D/prs\" -d':FL::UN:' s.f"
s "prs: the flag keywords, unset" "none no none no no no none  f none none none no" 0 "$mk; \"\$D/prs\" -d':Y: :MF: :MP: :KF: :BF: :J: :LK: :Q: :M: :FB: :CB: :Ds: :ND:' s.f"
s "prs: the flag keywords, set" "typ yes chk yes yes yes 2 qq mod 1 9 1.1 yes" 0 "$mk; \"\$D/admin\" -fttyp -fvchk -fi -fb -fj -fl2 -fqqq -fmmod -ff1 -fc9 -fd1.1 -fn s.f; \"\$D/prs\" -d':Y: :MF: :MP: :KF: :BF: :J: :LK: :Q: :M: :FB: :CB: :Ds: :ND:' s.f"
s "prs: :FD:" "none" 0 "$mk; \"\$D/prs\" -d:FD: s.f"
s "prs: :BD: the body, with its control lines" "2 3" 0 "$mk; \"\$D/prs\" -d:BD: s.f > b; printf '%s %s' \$(grep -c \"\$(printf '\\001')[IE] 1\" b) \$(grep -c '^[abc]\$' b)"
s "prs: :GB: the text" "f 1.1${nl}a${nl}b${nl}c" 0 "$mk; $ed1; \"\$D/prs\" -d:GB: -r1.1 s.f | sed '/^\$/d'"
s "prs: :W: :A: :Z: :F: :PN:" "@(#)f	1.1 @(#)none f 1.1@(#) @(#) s.f ./s.f" 0 "$mk; \"\$D/prs\" -d':W: :A: :Z: :F: :PN:' ./s.f"
s "prs: :Dn: :Dx: :Dg: :DI: empty" "   " 0 "$mk; \"\$D/prs\" -d':Dn: :Dx: :Dg: :DI:' s.f"
s "prs: an unknown keyword stays" ":XY: 1.1" 0 "$mk; \"\$D/prs\" -d':XY: :I:' s.f"
s "prs -a: removed deltas too" "1.2 R${nl}1.1 D" 0 "$mk; $ed1; \"\$D/rmdel\" -r1.2 s.f; \"\$D/prs\" -a -e -r1.2 -d':I: :DT:' s.f"
s "prs -e with -l: a usage error" "" 2 "$mk; \"\$D/prs\" -e -l -r1.1 s.f 2>/dev/null"
s "prs -e without -r or -c: a usage error" "" 2 "$mk; \"\$D/prs\" -e s.f 2>/dev/null"

# ---- rmdel ----
s "rmdel: the delta is removed" "R 1.2" 0 "$mk; $ed1; \"\$D/rmdel\" -r1.2 s.f; \"\$D/prs\" -a -r1.2 -d':DT: :I:' s.f"
s "rmdel: its text is gone" "f 1.1${nl}a${nl}b${nl}c" 0 "$mk; $ed1; \"\$D/rmdel\" -r1.2 s.f; \"\$D/get\" -p s.f 2>/dev/null"
s "rmdel: the SID can be made again" "new delta 1.2" 0 "$mk; $ed1; \"\$D/rmdel\" -r1.2 s.f; \"\$D/get\" -e -g s.f | sed -n 2p"
s "rmdel: not a leaf" "" 1 "$mk; $ed1; \"\$D/rmdel\" -r1.1 s.f 2>/dev/null"
s "rmdel: being edited" "" 1 "$mk; $ed1; \"\$D/get\" -e -g -s s.f; \"\$D/rmdel\" -r1.2 s.f 2>/dev/null"
s "rmdel: a SID that does not exist" "" 1 "$mk; \"\$D/rmdel\" -r1.5 s.f 2>/dev/null"
s "rmdel: no -r" "" 2 "$mk; \"\$D/rmdel\" s.f 2>/dev/null"
s "rmdel: the file still checks" "" 0 "$mk; $ed1; \"\$D/rmdel\" -r1.2 s.f; \"\$D/admin\" -h s.f && \"\$D/val\" s.f"

# ---- sact and unget ----
s "sact: the edits" "1.1 1.2 $who DATE" 0 "$mk; \"\$D/get\" -e -g -s s.f; \"\$D/sact\" s.f | sed \"\$ND\""
s "sact: none" "" 0 "$mk; \"\$D/sact\" s.f"
s "sact: several files" "${nl}s.a:${nl}1.1 1.2 $who DATE${nl}${nl}s.b:" 0 "$mk; mv s.f s.a; $mk; mv s.f s.b; \"\$D/get\" -e -g -s s.a; \"\$D/sact\" s.a s.b | sed \"\$ND\""
s "sact: a missing file" "" 1 "\"\$D/sact\" s.none 2>/dev/null"
s "unget: the SID, the g-file and the p-file go" "1.2${nl}s.f" 0 "$mk; \"\$D/get\" -e -s s.f; \"\$D/unget\" s.f; ls"
s "unget -n: the g-file stays" "f s.f" 0 "$mk; \"\$D/get\" -e -s s.f; \"\$D/unget\" -n -s s.f; echo *"
s "unget -s: nothing written" "" 0 "$mk; \"\$D/get\" -e -s s.f; \"\$D/unget\" -s s.f"
s "unget -r: one of two" "1.1.1.1${nl}1.1 1.2 $who DATE" 0 "$mk; \"\$D/admin\" -fj s.f; \"\$D/get\" -e -s -g s.f; \"\$D/get\" -e -s -g s.f; \"\$D/unget\" -r1.1.1.1 s.f; sed \"\$ND\" p.f"
s "unget: no edit" "" 1 "$mk; \"\$D/unget\" s.f 2>/dev/null"

# ---- val ----
s "val: a good file" "" 0 "$mk; \"\$D/val\" s.f"
s "val: no file (0x80)" "" 128 "\"\$D/val\" >/dev/null"
s "val: an unknown option (0x40)" "" 64 "$mk; \"\$D/val\" -x s.f >/dev/null"
s "val: an option twice (0x40)" "" 64 "$mk; \"\$D/val\" -mf -mf s.f >/dev/null"
s "val: a corrupted file (0x20)" "" 32 "$mk; chmod u+w s.f; sed 's/^a\$/A/' s.f > t && cat t > s.f; \"\$D/val\" s.f >/dev/null"
s "val: not an SCCS file (0x10)" "" 16 "echo x > s.x; \"\$D/val\" s.x s.none >/dev/null"
s "val: an ambiguous SID (0x08)" "" 8 "$mk; \"\$D/val\" -r1 s.f >/dev/null"
s "val: an invalid SID (0x08)" "" 8 "$mk; \"\$D/val\" -r1.0 s.f >/dev/null"
s "val: a SID that does not exist (0x04)" "" 4 "$mk; \"\$D/val\" -r1.5 s.f >/dev/null"
s "val: -y mismatch (0x02)" "" 2 "$mk; \"\$D/admin\" -fttyp s.f; \"\$D/val\" -yother s.f >/dev/null"
s "val: -m mismatch (0x01)" "" 1 "$mk; \"\$D/val\" -mother s.f >/dev/null"
s "val: -m and -y match" "" 0 "$mk; \"\$D/admin\" -fttyp s.f; \"\$D/val\" -mf -ytyp -r1.1 s.f"
s "val: the bits together" "" 7 "$mk; \"\$D/val\" -mx -yy -r1.9 s.f >/dev/null"
s "val: the message" "s.f: SID does not exist" 4 "$mk; \"\$D/val\" -r1.9 s.f"
s "val -s: no message" "" 4 "$mk; \"\$D/val\" -s -r1.9 s.f"
s "val -: lines of arguments" "-r1.9 s.f${nl}${nl} s.f: SID does not exist" 4 "$mk; printf -- '-r1.1 s.f\n-r1.9 s.f\n' | \"\$D/val\" -"

# ---- what ----
s "what: the strings" "f:${nl}	one${nl}	two" 0 "printf 'x@(#)one\nyy@(#)two\"z\n' > f; \"\$D/what\" f"
s "what: the ends of a string" "f:${nl}	a${nl}	b${nl}	c${nl}	d" 0 "printf '@(#)a>@(#)b\\\\\\\\@(#)c\\000@(#)d' > f; \"\$D/what\" f"
s "what -s: the first only" "f:${nl}	one" 0 "printf '@(#)one\n@(#)two\n' > f; \"\$D/what\" -s f"
s "what: none found: status 1" "f:" 1 "echo nothing > f; \"\$D/what\" f"
s "what: several files" "a:${nl}	x${nl}b:${nl}	y" 0 "printf '@(#)x' > a; printf '@(#)y' > b; \"\$D/what\" a b"
s "what: a missing file" "" 1 "\"\$D/what\" none 2>/dev/null"
s "what: on a gotten file" "f:${nl}	f	1.1" 0 "printf '%%W%%\n' > g; \"\$D/admin\" -ig s.f; \"\$D/get\" -s s.f; \"\$D/what\" f"

# ---- sccs ----
s "sccs create: SCCS/s.name, ,name, the g-file" ",f SCCS f SCCS/s.f" 0 "mkdir SCCS; printf '%%M%%\n' > f; \"\$U\" create f >/dev/null; echo * SCCS/*"
s "sccs edit and delget" "1.2${nl}f 1.2" 0 "mkdir SCCS; printf '%%M%% %%I%%\n' > f; \"\$U\" create f >/dev/null; \"\$U\" edit f >/dev/null; echo x >> f; \"\$U\" delget -yok f >/dev/null; \"\$U\" prs -d:I: f; sed 1q f"
s "sccs deledit: still being edited" "1.2 1.3 $who DATE" 0 "mkdir SCCS; printf '%%M%% %%I%%\n' > f; \"\$U\" create f >/dev/null; \"\$U\" edit f >/dev/null; echo x >> f; \"\$U\" deledit -yok f >/dev/null; sed \"\$ND\" SCCS/p.f"
s "sccs info, check, tell" "           f: being edited: 1.1 1.2 $who DATE${nl}1${nl}f" 0 "mkdir SCCS; printf 'x\n' > f; \"\$U\" create f >/dev/null 2>&1; \"\$U\" edit f >/dev/null; \"\$U\" info | sed \"\$ND\"; \"\$U\" check >/dev/null; echo \$?; \"\$U\" tell"
s "sccs info, check: nothing" "Nothing being edited${nl}0" 0 "mkdir SCCS; printf 'x\n' > f; \"\$U\" create f >/dev/null 2>&1; \"\$U\" info; \"\$U\" check; echo \$?"
s "sccs tell -u: only the user" "" 0 "mkdir SCCS; printf 'x\n' > f; \"\$U\" create f >/dev/null 2>&1; \"\$U\" edit f >/dev/null; \"\$U\" tell -u nobody-at-all"
s "sccs unedit" "           f: removed${nl}1.1${nl}1 lines${nl}none" 0 "mkdir SCCS; printf 'x\n' > f; \"\$U\" create f >/dev/null 2>&1; \"\$U\" edit f >/dev/null; \"\$U\" unedit f 2>/dev/null; [ -e SCCS/p.f ] || echo none"
s "sccs unedit: not being edited" "           f: not being edited" 0 "mkdir SCCS; printf 'x\n' > f; \"\$U\" create f >/dev/null 2>&1; \"\$U\" unedit f"
s "sccs clean: what is not being edited" "SCCS g" 0 "mkdir SCCS; printf 'x\n' > f; printf 'y\n' > g; \"\$U\" create f g >/dev/null 2>&1; rm ,f ,g; \"\$U\" edit g >/dev/null; \"\$U\" clean; echo *"
s "sccs diffs" "${nl}------- f -------${nl}1a2${nl}> y" 0 "mkdir SCCS; printf 'x\n' > f; \"\$U\" create f >/dev/null 2>&1; \"\$U\" edit f >/dev/null; echo y >> f; \"\$U\" diffs f"
s "sccs fix: the delta undone, its changes kept" "1.1 1.2 $who DATE${nl}x${nl}y" 0 "mkdir SCCS; printf 'x\n' > f; \"\$U\" create f >/dev/null 2>&1; \"\$U\" edit f >/dev/null; echo y >> f; \"\$U\" delget -y f >/dev/null 2>&1; \"\$U\" fix -r1.2 f >/dev/null 2>&1; sed \"\$ND\" SCCS/p.f; cat f"
s "sccs: the name made with -d and -p" "1.1" 0 "mkdir -p top/a/y; printf 'x\n' > g; \"\$D/admin\" -ig top/a/y/s.b 2>/dev/null; \"\$U\" -d top -p y prs -d:I: a/b"
s "sccs: an s. name is left as it is" "1.1" 0 "printf 'x\n' > g; \"\$D/admin\" -ig s.g 2>/dev/null; \"\$U\" prs -d:I: s.g"
s "sccs: PROJECTDIR" "1.1" 0 "mkdir -p proj/SCCS; printf 'x\n' > g; \"\$D/admin\" -ig proj/SCCS/s.g 2>/dev/null; PROJECTDIR=\$PWD/proj \"\$U\" prs -d:I: g"
s "sccs print: prs" "SCCS/s.f:" 0 "mkdir SCCS; printf 'x\n' > f; \"\$U\" create f >/dev/null 2>&1; \"\$U\" print f | sed 1q"
s "sccs: option-arguments are not names" "1.1" 0 "mkdir SCCS; printf 'x\\n' > f; \"\$U\" create f >/dev/null 2>&1; \"\$U\" get -p -r 1.1 f 2>&1 >/dev/null | grep -v warning | sed 1q"
s "sccs: an unknown command" "" 2 "\"\$U\" frobnicate f 2>/dev/null"
s "sccs: no command" "" 2 "\"\$U\" 2>/dev/null"

# options
cd "$T" || exit 2
# getopt() must not take an option after an operand for one (no permuting)
s "an option after an operand is an operand" "" 1 "$mk; \"\$D/get\" -s s.f -p 2>/dev/null"
s "get: an unknown option" "" 2 "\"\$D/get\" -z s.f 2>/dev/null"

done_tests sccs
