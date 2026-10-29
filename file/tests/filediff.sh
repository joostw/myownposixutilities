#!/bin/sh
# filediff.sh - compare file with the file of the system (Christos
# Zoulas's file, as on most systems): the files of some system
# directories, with links followed by both, each put in a coarse class
# by the words of the two outputs (ELF executable, ELF shared object,
# text, directory, archives, compressed data, images, data, ...), which
# must agree.
# Run: sh tests/filediff.sh ./file   (needs file in PATH)
F=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
command -v file >/dev/null || { echo "filediff: the system file is needed"; exit 2; }
file --version 2>/dev/null | grep -q '^file-' || { echo "filediff: the system file is not Zoulas's"; exit 2; }
export LC_ALL=C
T=${TMPDIR:-/tmp}/filediff.$$
trap 'rm -f "$T".*' EXIT
# class: one word for an output line (without "name: ")
class='
    /symbolic link/ { print "link"; next }
    /^directory/ { print "directory"; next }
    /^empty/ { print "empty"; next }
    /^fifo/ { print "fifo"; next }
    /^character special/ { print "char"; next }
    /^block special/ { print "block"; next }
    /ELF .*(pie executable|shared object)/ { print "elf-dyn"; next }
    /ELF .*executable/ { print "elf-exec"; next }
    /ELF .*relocatable/ { print "elf-rel"; next }
    /ELF/ { print "elf"; next }
    /ar archive/ { print "ar"; next }
    /tar archive/ { print "tar"; next }
    /cpio archive/ { print "cpio"; next }
    /gzip compressed/ { print "gzip"; next }
    /compress.d data/ { print "compress"; next }
    /bzip2 compressed/ { print "bzip2"; next }
    /XZ compressed/ { print "xz"; next }
    /Zstandard/ { print "zstd"; next }
    /Zip archive/ { print "zip"; next }
    /PNG image/ { print "png"; next }
    /GIF image/ { print "gif"; next }
    /JPEG image/ { print "jpeg"; next }
    /PDF document/ { print "pdf"; next }
    /text/ { print "text"; next }
    { print "other" }'
pass=0 fail=0 skip=0
for d in /usr/bin /usr/lib/x86_64-linux-gnu /etc /usr/include /usr/share/doc /usr/share/misc /usr/share/pixmaps /usr/share/icons; do
    [ -d "$d" ] || continue
    find "$d" -maxdepth 2 2>/dev/null | sed 400q
done > "$T.list"
while IFS= read -r f; do
    a=$("$F" "$f" 2>/dev/null | sed 's/^[^:]*: //' | awk "$class")
    b=$(file -L -b "$f" 2>/dev/null | awk "$class")
    # the system file has many more classes: compare only where it gave
    # one of ours, and count the "other" ones as skipped
    if [ "$b" = other ]; then skip=$((skip+1))
    elif [ "$a" = "$b" ]; then pass=$((pass+1))
    else fail=$((fail+1)); echo "FAIL $f: $a ($("$F" "$f" | sed 's/^[^:]*: //')) and $b ($(file -L -b "$f"))"; fi
done < "$T.list"
echo "filediff: $pass passed, $fail failed ($skip of other kinds skipped)"
[ "$fail" = 0 ]
