#!/bin/sh
# test_getconf.sh - tests for getconf (POSIX.1-2024).  Run: sh test_getconf.sh
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
U=${U:-$PWD/getconf}
T=${TMPDIR:-/tmp}/test_getconf.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# the reference: Python's os.sysconf(), os.confstr() and os.pathconf(),
# for each variable that Python has a name for
cat > ref.py <<'PY'
import os, subprocess, sys
u = sys.argv[1]
names = {"sys": [], "str": [], "path": []}
for line in sys.stdin:
    k, n = line.split()
    names[k].append(n)
bad = checked = 0
def check(args, want):
    global bad, checked
    p = subprocess.run([u] + args, capture_output=True, text=True)
    checked += 1
    if p.returncode != 0 or p.stdout != want + "\n":
        bad += 1
        print("differs:", " ".join(args), repr(p.stdout), p.returncode, "want", repr(want))
def sc_name(n):
    s = n.lstrip("_")
    for a, b in (("POSIX2_", "2_"), ("POSIX_", ""), ("PTHREAD_", "THREAD_"), ("XOPEN_", "XOPEN_")):
        if s.startswith(a):
            return "SC_" + b + s[len(a):]
    return "SC_" + s
for n in names["sys"]:
    k = sc_name(n)
    if k not in os.sysconf_names:
        continue
    try:
        v = os.sysconf(k)
    except (OSError, ValueError):
        continue
    check([n], "undefined" if v == -1 else str(v))
for n in names["str"]:
    k = "CS_" + n
    if k not in os.confstr_names:
        continue
    try:
        v = os.confstr(k)
    except (OSError, ValueError):
        continue
    check([n], "undefined" if v is None else v)
for n in names["path"]:
    k = "PC_" + n.lstrip("_").replace("POSIX_", "", 1) if n.startswith("_POSIX_") else "PC_" + n.replace("POSIX_", "", 1).replace("POSIX2_", "2_")
    if k not in os.pathconf_names:
        continue
    for p in ("/", "."):
        try:
            v = os.pathconf(p, k)
        except (OSError, ValueError):
            continue
        check([n, p], "undefined" if v == -1 else str(v))
print(checked >= 60 and bad == 0 and "ok" or "checked %d, %d differ" % (checked, bad))
PY
src=${U%/*}/getconf.c
t "every variable Python knows, as Python finds it" "ok" 0 sh -c '
sed -n "/^static const struct var sysvars/,/^};/s/^    { \"\([A-Z0-9_]*\)\".*/sys \1/p
/^static const struct var strvars/,/^};/s/^    { \"\([A-Z0-9_]*\)\".*/str \1/p
/^static const struct var pathvars/,/^};/s/^    { \"\([A-Z0-9_]*\)\".*/path \1/p" "$2" | sort -u | python3 ref.py "$1"' sh "$U" "$src"
# the system's getconf, for a few
t "ARG_MAX, as the system's getconf" "$(getconf ARG_MAX)" 0 "$U" ARG_MAX
t "PATH, as the system's getconf" "$(getconf PATH)" 0 "$U" PATH
t "NAME_MAX /, as the system's getconf" "$(getconf NAME_MAX /)" 0 "$U" NAME_MAX /
t "_POSIX_VERSION, as the system's getconf" "$(getconf _POSIX_VERSION)" 0 "$U" _POSIX_VERSION
t "LINE_MAX: at least _POSIX2_LINE_MAX" "yes" 0 sh -c '[ "$("$1" LINE_MAX)" -ge 2048 ] && echo yes' sh "$U"
t "PATH finds the standard utilities" "yes" 0 sh -c 'p=$("$1" PATH); PATH=$p command -v sh >/dev/null && echo yes' sh "$U"
# the values the standard fixes
t "_POSIX_ARG_MAX" "4096" 0 "$U" _POSIX_ARG_MAX
t "_POSIX_NAME_MAX" "14" 0 "$U" _POSIX_NAME_MAX
t "_POSIX_PATH_MAX" "256" 0 "$U" _POSIX_PATH_MAX
t "_POSIX2_LINE_MAX" "2048" 0 "$U" _POSIX2_LINE_MAX
t "_POSIX_CLOCKRES_MIN" "20000000" 0 "$U" _POSIX_CLOCKRES_MIN
t "_XOPEN_IOV_MAX" "16" 0 "$U" _XOPEN_IOV_MAX
t "_POSIX_TZNAME_MAX" "6" 0 "$U" _POSIX_TZNAME_MAX
# the earlier names
t "POSIX2_LINE_MAX is _POSIX2_LINE_MAX" "2048" 0 "$U" POSIX2_LINE_MAX
t "POSIX2_BC_BASE_MAX is _POSIX2_BC_BASE_MAX" "99" 0 "$U" POSIX2_BC_BASE_MAX
t "POSIX2_VERSION is _POSIX2_VERSION" "$("$U" _POSIX2_VERSION)" 0 "$U" POSIX2_VERSION
t "POSIX2_C_BIND is _POSIX2_C_BIND" "$("$U" _POSIX2_C_BIND)" 0 "$U" POSIX2_C_BIND
t "NPROCESSORS_ONLN: a number" "yes" 0 sh -c '[ "$("$1" NPROCESSORS_ONLN)" -ge 1 ] && echo yes' sh "$U"
t "NPROCESSORS_CONF: a number" "yes" 0 sh -c '[ "$("$1" NPROCESSORS_CONF)" -ge 1 ] && echo yes' sh "$U"
# the V8 environments: one of them is there
t "one of the V8 environments is supported" "yes" 0 sh -c 'for e in ILP32_OFF32 ILP32_OFFBIG LP64_OFF64 LPBIG_OFFBIG; do v=$("$1" _POSIX_V8_$e); case $v in -1|undefined) ;; *) echo yes; exit;; esac; done' sh "$U"
t "-v with a supported environment" "$("$U" PAGESIZE)" 0 sh -c 'for e in ILP32_OFF32 ILP32_OFFBIG LP64_OFF64 LPBIG_OFFBIG; do v=$("$1" _POSIX_V8_$e); case $v in -1|undefined) ;; *) exec "$1" -v POSIX_V8_$e PAGESIZE;; esac; done' sh "$U"
t "-v with a specification that is none" "" 1 "$U" -v POSIX_V8_NONE PAGESIZE
t "-v with an empty specification" "" 1 "$U" -v "" PAGESIZE
# errors: nothing on the standard output
t "an unknown variable" "" 1 "$U" NO_SUCH_VARIABLE
t "an unknown variable: a diagnostic" "" 0 sh -c '"$1" NO_SUCH_VARIABLE 2>&1 | grep -q .' sh "$U"
t "a path variable without a pathname" "" 1 "$U" NAME_MAX
t "a system variable with a pathname" "" 1 "$U" ARG_MAX /
t "a missing pathname" "" 1 "$U" NAME_MAX ./no_such_file
t "_SC_ARG_MAX is not a name" "" 1 "$U" _SC_ARG_MAX
t "a name in lower case" "" 1 "$U" arg_max
t "no operand" "" 2 "$U"
t "three operands" "" 2 "$U" NAME_MAX / /
t "-v without a specification" "" 2 "$U" -v
t "an unknown option" "" 2 "$U" -x ARG_MAX
t "--" "$("$U" ARG_MAX)" 0 "$U" -- ARG_MAX
t "a path variable for a file" "$("$U" NAME_MAX .)" 0 sh -c ': > f; "$1" NAME_MAX f' sh "$U"
t "a path variable for a FIFO: PIPE_BUF" "yes" 0 sh -c 'mkfifo p && [ "$("$1" PIPE_BUF p)" -ge 512 ] && echo yes' sh "$U"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: getconf ARG_MAX -v: -v is a pathname" "" 1 "$U" ARG_MAX -v
[ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" ARG_MAX > /dev/full' sh "$U"

done_tests getconf
