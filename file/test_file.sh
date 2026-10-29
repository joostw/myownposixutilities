#!/bin/sh
# test_file.sh - tests for file (POSIX.1-2024).  Run: sh test_file.sh
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
U=${U:-$PWD/file}
T=${TMPDIR:-/tmp}/test_file.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# z FILE SIZE: FILE made of SIZE null bytes; p FILE OFFSET STRING: STRING
# (printf format) written into FILE at OFFSET
z() { dd if=/dev/zero of="$1" bs="$2" count=1 2>/dev/null; }
p() { printf "$3" | dd of="$1" bs=1 seek="$2" conv=notrunc 2>/dev/null; }
# r NAME EXPECTED OPTIONS-AND-FILE...: the output of file
r() { _n=$1 _e=$2; shift 2; t "$_n" "$_e" 0 "$U" "$@"; }

printf 'plain text\n' > text
printf 'caf\303\251 cr\303\250me\n' > utf8
printf 'caf\351\n' > latin
printf 'a\001\002\003binary\000\377\376\n' > bin
: > empty
mkdir dir
mkfifo fifo
ln -s text link
ln -s nowhere dangling
printf '#!/bin/sh\necho hello\n' > script
printf '#! /usr/bin/env ksh\nprint hello\n' > envscript
printf '#!/usr/bin/awk -f\n{ print }\n' > awkscript
printf 'cd /tmp || exit\nfor f in *; do\n    echo "$f"\ndone\n' > shell
printf '#include <stdio.h>\n\nint main(void)\n{\n    puts("hi");\n    return 0;\n}\n' > prog.c
printf 'static int count;\nint next(void) { return ++count; }\n' > noinc.c
printf 'C     A COMMENT\n      PROGRAM HELLO\n      INTEGER I\n      DO 10 I = 1, 3\n      PRINT *, I\n   10 CONTINUE\n      END\n' > hello.f
printf 'program free\n  implicit none\n  print *, "free form"\nend program free\n' > free.f90

# file types
r "a directory" "dir: directory" dir
r "a fifo" "fifo: fifo" fifo
r "a character special" "/dev/null: character special" /dev/null
r "a nonexistent file: cannot open" "yes" nonexistent | true
t "a nonexistent file: cannot open, status 0" "yes" 0 sh -c '"$1" nonexistent | grep -q "^nonexistent: cannot open" && echo yes' sh "$U"
r "an empty file" "empty: empty" empty
r "a symbolic link is followed" "link: ASCII text" link
r "-h: the link itself" "link: symbolic link to text" -h link
r "a dangling link: a link even without -h" "dangling: symbolic link to nowhere" dangling
if ! $root; then
    t "a file that cannot be read" "yes" 0 sh -c 'echo x > unread; chmod 0 unread; "$1" unread | grep -q "^unread: cannot open" && echo yes' sh "$U"
fi
t "a socket" "yes" 0 sh -c 'python3 -c "import socket, sys; s = socket.socket(socket.AF_UNIX); s.bind(\"sock\")" 2>/dev/null || exit 0; "$1" sock | grep -q "^sock: socket\$" && echo yes || echo no' sh "$U"
t "a block special" "yes" 0 sh -c 'b=$(ls /dev 2>/dev/null | while read d; do [ -b /dev/$d ] && { echo /dev/$d; break; }; done); [ -n "$b" ] || { echo yes; exit; }; "$1" $b | grep -q ": block special\$" && echo yes' sh "$U"

# -i
r "-i: a regular file" "text: regular file" -i text
r "-i: an empty file is just a regular file" "empty: regular file" -i empty
r "-i: a directory" "dir: directory" -i dir
r "-i -h: a link" "link: symbolic link to text" -i -h link
r "-i: a link followed" "link: regular file" -i link

# text
r "ASCII text" "text: ASCII text" text
r "UTF-8 text" "utf8: UTF-8 text" utf8
r "ISO-8859 text" "latin: ISO-8859 text" latin
r "binary data" "bin: data" bin
r "a shell script" "script: sh script, commands text" script
r "a script by env" "envscript: ksh script, commands text" envscript
r "another interpreter" "awkscript: awk script, ASCII text" awkscript
r "shell commands without #!" "shell: commands text" shell
r "C source" "prog.c: c program text" prog.c
r "C source without #include" "noinc.c: c program text" noinc.c
r "FORTRAN, fixed form" "hello.f: fortran program text" hello.f
r "FORTRAN, free form" "free.f90: fortran program text" free.f90
r "several operands" "text: ASCII text${nl}dir: directory" text dir
t "-: the standard input" "-: c program text" 0 sh -c '"$1" - < prog.c' sh "$U"

# the default position-sensitive tests
t "an executable binary" "yes" 0 sh -c '"$1" "$1" | grep -q "executable" && echo yes' sh "$U"
ok "an ar archive" sh -c 'cp text m.o; ar -rc lib.a m.o 2>/dev/null || exit 0; "$1" lib.a | grep -q "archive"' sh "$U"
z odc 512; p odc 0 '070707000001'
r "a cpio archive (odc)" "odc: ASCII cpio archive (pre-SVR4 or odc)" odc
z newc 512; p newc 0 '070701'
r "a cpio archive (newc)" "newc: ASCII cpio archive (SVR4 with no CRC)" newc
z bcpio 512; p bcpio 0 '\307\161'
r "a binary cpio archive" "bcpio: cpio archive (binary, little-endian)" bcpio
z ustar 1024; p ustar 0 'text'; p ustar 156 0; p ustar 257 'ustar\00000'
r "a tar archive (ustar)" "ustar: POSIX tar archive" ustar
z pax 1024; p pax 0 'PaxHeader'; p pax 156 x; p pax 257 'ustar\00000'
r "a pax archive" "pax: POSIX tar archive (pax)" pax
z gnutar 1024; p gnutar 0 'text'; p gnutar 257 'ustar  \000'
r "a GNU tar archive" "gnutar: GNU tar archive" gnutar
# a V7 header: the checksum of the block, with the field as spaces
z v7 1024; p v7 0 'f'; p v7 100 '0000644\000'; p v7 124 '00000000000\000'; p v7 148 '        '
sum=$(od -An -v -tu1 -N 512 v7 | tr -s ' ' '\n' | awk 'NF { s += $1 } END { print s }')
p v7 148 "$(printf '%06o\\000 ' "$sum")"
r "a tar archive (V7): the checksum" "v7: tar archive (V7)" v7
p v7 0 'g'
r "a bad checksum: not tar" "v7: data" v7
printf '\037\235\220abc' > comp
r "compress'd data" "comp: compress'd data 16 bits" comp
printf '\037\213\010\000' > gz
r "gzip data" "gz: gzip compressed data" gz
printf '%%PDF-1.7\n%%\342\343\n' > doc.pdf
r "PDF" "doc.pdf: PDF document, version 1.7" doc.pdf
printf '\211PNG\r\n\032\n\000\000\000\015IHDR\000\000\001\000\000\000\000\100' > img.png
r "PNG" "img.png: PNG image data, 256 x 64" img.png
printf 'GIF89a\040\000\020\000' > img.gif
r "GIF" "img.gif: GIF image data, version 89a, 32 x 16" img.gif
printf 'PK\003\004\024\000' > a.zip
r "Zip" "a.zip: Zip archive data" a.zip
printf '\177ELF\002\001\001\000\000\000\000\000\000\000\000\000\001\000\076\000' > elf; z pad 64; cat pad >> elf
r "ELF, relocatable" "elf: ELF 64-bit LSB relocatable, x86-64" elf

# -m and -M
# the native byte order: is 1 2 read as 0x0201?
le=false; [ "$(printf '\001\002' | od -An -tx2 | tr -d ' ')" = 0201 ] && le=true
tab=$(printf '\t')
cat > m1 <<'M'
# comments and empty lines are ignored

0	string	MAGIC1	the first kind
0	string	HDR	header
>3	byte	1	version 1
>3	byte	2	version 2
>4	string	X	with X
0	byte	0x7f	starts with DEL
M
printf 'MAGIC1 and more\n' > f1
printf 'HDR\001X\n' > f2
printf 'HDR\002Y\n' > f3
printf '\177rest\n' > f4
r "-m: a string" "f1: the first kind" -m m1 f1
r "-m: > lines add to the message" "f2: header version 1 with X" -m m1 f2
r "-m: only the > lines that match" "f3: header version 2" -m m1 f3
r "-m: a byte in hexadecimal" "f4: starts with DEL" -m m1 f4
r "-m: no match, then the defaults" "text: ASCII text" -m m1 text
r "-M: no match: no defaults, data" "text: data" -M m1 text
r "-M with -d: the defaults after" "text: ASCII text" -M m1 -d text
printf '0\tstring\t\\037\\213\tmy gzip\n' > m3
r "-d with -M: the defaults first" "gz: gzip compressed data" -d -M m3 gz
r "-M then -d: the magic first" "gz: my gzip" -M m3 -d gz
r "-d with -M: the context-sensitive tests last" "f1: the first kind" -d -M m1 f1
r "-m: the file types first" "dir: directory" -m m1 dir
r "-m: an empty file is empty" "empty: empty" -m m1 empty
cat > m2 <<'M'
0	string	MAGIC1	from m2
0	string	OTHER	other
M
r "-m twice: in order" "f1: the first kind" -m m1 -m m2 f1
r "-m twice: the other order" "f1: from m2" -m m2 -m m1 f1
printf 'OTHER\n' > f5
r "-m twice: the second file" "f5: other" -m m1 -m m2 f5

# the fields: offsets, types, values, comparisons, masks, messages
printf 'ab\001\377\000\010zz\nxy' > v
# n NAME EXPECTED LINE: file -M with the one line LINE on v
n() { printf '%s\n' "$3" > mm; t "$1" "$2" 0 "$U" -M mm v; }
n "offset in decimal" "v: yes" "1${tab}string${tab}b${tab}yes"
n "offset in octal" "v: yes" "011${tab}string${tab}xy${tab}yes"
n "offset in hexadecimal" "v: yes" "0x6${tab}string${tab}zz${tab}yes"
n "offset past the end: fails" "v: data" "100${tab}byte${tab}x${tab}yes"
n "a string past the end: fails" "v: data" "9${tab}string${tab}xyz${tab}yes"
n "string escapes" "v: yes" "8${tab}string${tab}\\nxy${tab}yes"
n "string octal escapes" "v: yes" "2${tab}string${tab}\\001\\377${tab}yes"
n "a string with a space (tab separated)" "v: yes" "0${tab}string${tab}ab${tab}yes"
n "blank separated fields" "v: yes" "0 string ab yes"
n "\\\\ (a space) in a string" "v: data" '0 string a\ b yes'
n "dC: signed byte" "v: -1" "3${tab}dC${tab}-1${tab}%d"
n "uC: unsigned byte" "v: 255" "3${tab}uC${tab}255${tab}%d"
n "d1: signed byte" "v: yes" "3${tab}d1${tab}<0${tab}yes"
n "u1: unsigned byte" "v: yes" "3${tab}u1${tab}>200${tab}yes"
n "byte: dC" "v: yes" "2${tab}byte${tab}1${tab}yes"
n "& in the value: all bits set" "v: yes" "5${tab}byte${tab}&8${tab}yes"
n "& in the value: not all set" "v: data" "5${tab}byte${tab}&9${tab}yes"
n "^: a bit not set" "v: yes" "5${tab}byte${tab}^9${tab}yes"
n "^: all set" "v: data" "5${tab}byte${tab}^8${tab}yes"
n "a mask" "v: yes" "3${tab}uC&0x0f${tab}15${tab}yes"
n "a mask in octal" "v: yes" "3${tab}uC&017${tab}=15${tab}yes"
n "x: any value" "v: 97" "0${tab}uC${tab}x${tab}%d"
n "=: equal" "v: yes" "0${tab}uC${tab}=97${tab}yes"
n "a value in hexadecimal" "v: yes" "0${tab}uC${tab}0x61${tab}yes"
n "a value in octal" "v: yes" "0${tab}uC${tab}0141${tab}yes"
n "<: less" "v: data" "0${tab}uC${tab}<97${tab}yes"
n ">: greater" "v: yes" "0${tab}uC${tab}>96${tab}yes"
n "the message: %x" "v: ff" "3${tab}uC${tab}x${tab}%x"
n "the message: %c" "v: a" "0${tab}uC${tab}x${tab}%c"
n "the message: %s of a string" "v: [ab]" "0${tab}string${tab}ab${tab}[%s]"
n "the message: %% and width" "v: 100% |   97|" "0${tab}uC${tab}x${tab}100%% |%5d|"
if $le; then
    n "dS: two bytes, native order (little-endian)" "v: 25185" "0${tab}dS${tab}x${tab}%d"
    n "d2: the same" "v: yes" "0${tab}d2${tab}0x6261${tab}yes"
    n "uI: four bytes" "v: 4278280801" "0${tab}uI${tab}x${tab}%u" 
else
    n "dS: two bytes, native order (big-endian)" "v: 24930" "0${tab}dS${tab}x${tab}%d"
    n "d2: the same" "v: yes" "0${tab}d2${tab}0x6162${tab}yes"
    n "uI: four bytes" "v: 1633813055" "0${tab}uI${tab}x${tab}%u"
fi
n "d4 & mask, then compare" "v: yes" "0${tab}u4&0xff${tab}97${tab}yes"
n "short: dS" "v: yes" "4${tab}short${tab}x${tab}yes"
n "long: dL" "v: yes" "0${tab}long${tab}x${tab}yes"
n "\\b: no space before a message" "v: ab!" "$(printf '0\tstring\tab\tab\n>2\tbyte\t1\t\\b!')"
n "nested >>: only after a > that matched" "v: a b" "$(printf '0\tbyte\t97\ta\n>1\tbyte\t98\tb\n>>2\tbyte\t9\tc\n>1\tbyte\t99\td\n>>2\tbyte\t1\te')"
t "-M: a bad line is an error" "" 1 sh -c 'printf "0\tbad\tx\ty\n" > bad; "$1" -M bad v' sh "$U"
t "-M: a bad offset" "" 1 sh -c 'printf "zero\tbyte\t1\ty\n" > bad; "$1" -M bad v' sh "$U"
t "-M: a bad size" "" 1 sh -c 'printf "0\td3\t1\ty\n" > bad; "$1" -M bad v' sh "$U"
t "-m: a missing file" "" 1 "$U" -m nonexistent v

# options
t "no operands" "" 2 "$U"
t "-i with -m" "" 2 "$U" -i -m m1 text
t "-i with -d" "" 2 "$U" -i -d text
t "an unknown option" "" 2 "$U" -x text
# getopt() must not take an option after an operand for one (no permuting)
t "an option after an operand is an operand" "text: ASCII text${nl}-h: cannot open (No such file or directory)" 0 "$U" text -h

done_tests file
