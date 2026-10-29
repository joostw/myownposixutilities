#!/bin/sh
# test_find.sh - tests for find (POSIX.1-2024).  Run: sh test_find.sh
# The order of the entries of a directory is that of readdir(), so most
# outputs are sorted before they are compared.
cd "$(dirname "$0")" || exit 2
pass=0 fail=0
# t NAME EXPECTED-STDOUT EXPECTED-STATUS COMMAND...  (stdout compared without trailing newlines)
t() {
    _n=$1 _e=$2 _s=$3; shift 3
    _o=$("$@" 2>/dev/null); _r=$?
    if [ "$_o" = "$_e" ] && [ "$_r" = "$_s" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s] status %s\n  want: [%s] status %s\n' "$_n" "$_o" "$_r" "$_e" "$_s"; fi
}
# f NAME EXPECTED-SORTED-STDOUT EXPECTED-STATUS ARGUMENT...: find, its output sorted
f() {
    _n=$1 _e=$2 _s=$3; shift 3
    t "$_n" "$_e" "$_s" sh -c '"$0" "$@" > out; r=$?; sort out; exit $r' "$U" "$@"
}
done_tests() { echo "$1: $pass passed, $fail failed"; [ "$fail" = 0 ]; }
U=${U:-$PWD/find}
T=${TMPDIR:-/tmp}/test_find.$$
mkdir -m 700 "$T" || exit 2
# fstat: exact file status, which POSIX ls does not show (../testlib/fstat.c)
cc=${CC:-}
[ -n "$cc" ] || for c in c17 c99 cc; do command -v $c >/dev/null 2>&1 && { cc=$c; break; }; done
X=
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T" "$T.fstat" $X' EXIT
$cc -o "$T.fstat" ../testlib/fstat.c || { echo "cannot compile ../testlib/fstat.c"; exit 2; }
fstat() { "$T.fstat" "$@"; }
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'

mkdir -p t/a/b t/c
: > t/f; echo data > t/a/g; : > t/a/b/h; : > t/c/.hid; : > t/c/H.TXT
ln -s ../a t/c/la; ln -s nowhere t/dl; mkfifo t/p
chmod 644 t/f t/a/g t/a/b/h t/c/.hid t/c/H.TXT; chmod 4755 t/f; chmod 755 t t/a t/a/b t/c

all="t
t/a
t/a/b
t/a/b/h
t/a/g
t/c
t/c/.hid
t/c/H.TXT
t/c/la
t/dl
t/f
t/p"
f "no expression: -print" "$all" 0 t
f "-print" "$all" 0 t -print
f "-type f" "t/a/b/h${nl}t/a/g${nl}t/c/.hid${nl}t/c/H.TXT${nl}t/f" 0 t -type f
f "-type d" "t${nl}t/a${nl}t/a/b${nl}t/c" 0 t -type d
f "-type l" "t/c/la${nl}t/dl" 0 t -type l
f "-type p" "t/p" 0 t -type p
f "-type c" "/dev/null" 0 /dev/null -type c
f "-name" "t/a/b/h" 0 t -name h
f "-name a pattern" "t/a/b/h${nl}t/c/.hid" 0 t -name '*h*'
f "-name: a leading period matches *" "t/c/.hid" 0 t -name '*hid'
f "-name [ ]" "t/a${nl}t/a/b" 0 t -name '[ab]'
f "-iname" "t/c/H.TXT" 0 t -iname 'h.txt'
f "-path" "t/a/b${nl}t/a/b/h${nl}t/a/g" 0 t -path 't/a/*'
f "-path: * matches /" "t/a/b/h" 0 t -path 't*h'
f "!" "t/c/la${nl}t/dl${nl}t/p" 0 t ! -type d ! -type f
f "-o" "t/a/g${nl}t/f" 0 t -name f -o -name g
f "-a binds tighter than -o" "t/a/g${nl}t/f" 0 t -type f -name f -o -name g
f "( )" "t/f" 0 t "(" -name f -o -name g ")" -type f -perm -4000
f "-prune" "t${nl}t/c${nl}t/c/.hid${nl}t/c/H.TXT${nl}t/c/la${nl}t/dl${nl}t/f${nl}t/p" 0 t -name a -prune -o -print
f "-prune -o -type f -print" "t/c/.hid${nl}t/c/H.TXT${nl}t/f" 0 t -name a -prune -o -type f -print
f "-perm octal" "t/a/b/h${nl}t/a/g${nl}t/c/.hid${nl}t/c/H.TXT${nl}t/p" 0 t -perm 644
f "-perm -octal" "t/f" 0 t -perm -4000
f "-perm symbolic" "t/f" 0 t -perm -u=s
f "-perm symbolic, exactly" "t/a/b/h${nl}t/a/g${nl}t/c/.hid${nl}t/c/H.TXT${nl}t/p" 0 t -perm u=rw,go=r
f "-perm -mode: at least these bits" "t${nl}t/a${nl}t/a/b${nl}t/c${nl}t/f" 0 t ! -type l -perm -u=x
f "-links" "t/a/g" 0 t -type f -links 1 -name g
f "-links +n" "t${nl}t/a" 0 t -type d -links +2
f "-size 0" "t/a/b/h${nl}t/c/.hid${nl}t/c/H.TXT${nl}t/f" 0 t -type f -size 0
f "-size 1: in blocks, rounded up" "t/a/g" 0 t -type f -size 1
f "-size nc" "t/a/g" 0 t -type f -size 5c
f "-size -nc" "t/a/g" 0 t -type f -size +4c
f "-size -n" "t/a/b/h${nl}t/c/.hid${nl}t/c/H.TXT${nl}t/f" 0 t -type f -size -1
touch -t 200001010000 t/a/g
f "-mtime +n" "t/a/g" 0 t -mtime +100
f "-mtime 0" "t/a/b/h" 0 t -mtime 0 -name h
f "-mtime -n" "t/a/b/h" 0 t -mtime -1 -name h
f "-atime" "" 0 t -atime +100000
f "-ctime" "t/f" 0 t -ctime 0 -name f
touch t/a/b/h
f "-newer" "t/a/b/h" 0 t -type f -newer t/a/g ! -name f ! -name H.TXT ! -name .hid
uid=$(id -u); gid=$(id -g)
f "-user by number" "t/f" 0 t -name f -user "$uid"
f "-group by number" "t/f" 0 t -name f -group "$gid"
f "-user by name" "t/f" 0 t -name f -user "$(id -un)"
f "-nouser" "" 0 t -nouser
f "-nogroup" "" 0 t -nogroup
if $root; then
    chown 54321:54321 t/c/.hid
    f "-nouser: an unknown user" "t/c/.hid" 0 t -nouser
    f "-nogroup: an unknown group" "t/c/.hid" 0 t -nogroup
    chown 0:0 t/c/.hid
fi
f "-user: an unknown name" "" 2 t -user no_such_user_here

# order
mkdir -p o/x/y
t "directories before their entries" "o${nl}o/x${nl}o/x/y" 0 "$U" o
t "-depth: entries first" "o/x/y${nl}o/x${nl}o" 0 "$U" o -depth
t "-depth anywhere in the expression" "o/x/y${nl}o/x${nl}o" 0 "$U" o -print -depth
t "-depth: -prune does nothing" "o/x/y${nl}o/x${nl}o" 0 "$U" o -depth -prune

# pathnames
t "a trailing slash is kept" "o/${nl}o/x${nl}o/x/y" 0 "$U" o/
t "two trailing slashes" "o//${nl}o//x${nl}o//x/y" 0 "$U" o//
t "dot" ".${nl}./x${nl}./x/y" 0 sh -c 'cd o && "$1"' sh "$U"
t "several paths" "o/x${nl}o/x/y${nl}t/a/b${nl}t/a/b/h" 0 "$U" o/x t/a/b
t "-name of a path with a slash at the end" "o/" 0 "$U" o/ -name o
t "a missing path" "o/x/y" 1 "$U" nofile o/x/y
t "a file as the path" "t/f" 0 "$U" t/f

# symbolic links
f "without -H, -L: links are not followed" "t/c/la" 0 t/c/la
f "-H: an operand is followed" "t/c/la${nl}t/c/la/b${nl}t/c/la/b/h${nl}t/c/la/g" 0 -H t/c/la
f "-H: links below are not" "t/c/la" 0 -H t/c -type l -name la
f "-L: links below are followed" "t/c/la/b/h" 0 -L t/c -name h
f "-L: a link's type is that of its target" "t/c/la" 0 -L t/c -type d -name la
f "-L: a dangling link stays a link" "t/dl" 0 -L t -type l
f "-H -L: the last" "t/c/la/b/h" 0 -H -L t/c -name h
ln -s .. t/a/b/up
t "-L: a loop is reported" "" 1 sh -c '"$1" -L t > /dev/null' sh "$U"
f "-L: a loop: the rest is found" "t/a/b/h${nl}t/c/la/b/h" 1 -L t -name h
rm t/a/b/up

# -exec and -ok
f "-exec ;" "x t/a/g" 0 t -name g -exec echo x {} ";"
f "-exec ; is false when the command fails" "" 0 t -name g -exec false ";" -print
f "-exec ; and -print" "t/a/g" 0 t -name g -exec true ";" -print
f "-exec ; {} inside an argument" "[t/a/g]" 0 t -name g -exec echo "[{}]" ";"
t "-exec +" "x t/a/b/h t/a/g" 0 sh -c '"$1" t/a -type f -exec echo x {} + | { read -r x rest; printf "%s " "$x"; echo $rest | tr " " "\n" | sort | tr "\n" " " | sed "s/ \$//"; }' sh "$U"
t "-exec + aggregates: one command" "1" 0 sh -c '"$1" t -exec echo {} + | wc -l | tr -d " "' sh "$U"
t "-exec + failing: status 1" "" 1 "$U" t -name f -exec false {} +
t "-exec + is true" "t/f" 0 "$U" t -name f -exec true {} + -print
t "-exec: a + not after {} is an argument" "+ t/f" 0 "$U" t -name f -exec echo + {} ";"
t "-exec: the working directory" "$T" 0 "$U" t -name f -exec pwd ";"
t "-exec: no ;" "" 2 "$U" t -exec echo {}
t "-exec: no utility" "" 2 "$U" t -exec ";"
t "-ok yes" "t/f" 0 sh -c 'echo y | "$1" t -name f -ok echo {} ";"' sh "$U"
t "-ok no" "" 0 sh -c 'echo n | "$1" t -name f -ok echo {} ";"' sh "$U"
t "-ok the prompt" "< echo t/f >? " 0 sh -c 'echo n | "$1" t -name f -ok echo {} ";" 2>&1' sh "$U"
t "-ok no: false" "no" 0 sh -c 'echo n | "$1" t -name f ! -ok true ";" -exec echo no ";"' sh "$U"
awk 'BEGIN { for (i = 0; i < 3000; i++) printf "%0100d\n", i }' > names
mkdir many
(cd many && xargs touch < ../names)
t "-exec + with many long names: several commands, none lost" "3000" 0 sh -c '"$1" many -type f -exec sh -c "for f; do echo \$f; done" sh {} + | wc -l | tr -d " "' sh "$U"

# -print0
t "-print0" "t/a/g@" 0 sh -c '"$1" t -name g -print0 | tr "\0" @' sh "$U"
t "-print0 and no -print" "1" 0 sh -c '"$1" t -name f -print0 | tr "\0" "\n" | wc -l | tr -d " "' sh "$U"

# depth past PATH_MAX, few open files
name=$(awk 'BEGIN { while (i++ < 100) printf "n" }')
mkdir deep
(cd -P deep && i=0 && while [ $i -lt 60 ]; do mkdir "$name" && cd -P "$name" || exit 1; i=$((i+1)); done; : > leaf) || echo "could not make the tree"
t "deeper than PATH_MAX, 16 open files" "1" 0 sh -c 'ulimit -n 16 && "$1" deep -name leaf | wc -l | tr -d " "' sh "$U"
t "deeper than PATH_MAX: the length" "1" 0 sh -c '"$1" deep -name leaf | awk "length > 6000 { n++ } END { print n }"' sh "$U"
mkdir lim
(cd lim && i=0 && while [ $i -lt 150 ]; do mkdir a && cd a || exit 1; i=$((i+1)); done)
t "150 levels, -depth, 16 open files" "151" 0 sh -c 'ulimit -n 16 && "$1" lim -depth | wc -l | tr -d " "' sh "$U"

# -xdev and -mount, with /dev/shm if it is another file system
if [ -d /dev/shm ] && [ -w /dev/shm ] && [ "$(fstat %d /dev/shm)" != "$(fstat %d .)" ]; then
    W=/dev/shm/test_find.$$; X=$W
    mkdir -p "$W/sub"; : > "$W/sub/inside"
    mkdir xd; ln -s "$W" xd/lnk; : > xd/here
    f "-L without -xdev: into the other file system" "xd/lnk/sub/inside" 0 -L xd -name inside
    f "-L -xdev: not below the other file system" "xd/lnk" 0 -L xd -xdev -name lnk
    f "-L -xdev: the directory itself is acted on" "" 0 -L xd -xdev -name inside
    f "-L -mount: nor the directory itself" "" 0 -L xd -mount -name lnk
    f "-mount: the rest" "xd${nl}xd/here" 0 -L xd -mount
fi

# permissions
if ! $root; then
    mkdir -p nr/d; : > nr/d/x; chmod 0 nr/d
    t "an unreadable directory: reported, status 1" "nr${nl}nr/d" 1 "$U" nr
    chmod 755 nr/d
fi

# errors
t "not a primary" "" 2 "$U" t -bogus
t "a missing argument" "" 2 "$U" t -name
t "a missing )" "" 2 "$U" t "(" -print
t "a bad -type" "" 2 "$U" t -type x
t "a bad -size" "" 2 "$U" t -size 1k
t "a bad -perm" "" 2 "$U" t -perm u+q
[ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" t >/dev/full' sh "$U"

done_tests find
