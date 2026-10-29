#!/bin/sh
# Interoperability with GNU tar, bsdtar, GNU cpio and MirBSD pax (whichever
# are installed).  usage: tests/interop.sh ./pax
P=$(realpath "${1:-./pax}") || exit 2
HERE=$(cd "$(dirname "$0")" && pwd -P)
CMP="python3 $HERE/treecmp.py"
# a UTF-8 locale for names outside ASCII (POSIX names none): $UTF8_LOCALE,
# or C.UTF-8, or any one that locale -a lists; else the C locale
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
LC_ALL=${U8:-C} TZ=UTC
export LC_ALL TZ
W=${TMPDIR:-/tmp}/paxinterop.$$
mkdir -m 700 "$W" || exit 2
trap 'rm -rf "$W"' EXIT
cd "$W" || exit 2
# file times without following symbolic links (POSIX touch cannot)
cc=${CC:-}
[ -n "$cc" ] || for c in c17 c99 cc; do command -v $c >/dev/null 2>&1 && { cc=$c; break; }; done
$cc -o ltouch "$HERE/../../testlib/ltouch.c" || exit 2
LTOUCH=$W/ltouch

pass=0; fail=0
ok()  { pass=$((pass+1)); echo "ok   $*"; }
bad() { fail=$((fail+1)); echo "FAIL $*"; }
check() { # name flags tree1 tree2
    _n=$1 _f=$2; shift 2
    if _out=$($CMP $_f "$@" 2>&1); then ok "$_n"; else bad "$_n"; echo "$_out" | head -n 5 | cut -c1-160; fi
}
have() { command -v "$1" >/dev/null; }
MIRPAX=; [ -x /usr/bin/pax ] && [ "$(realpath /usr/bin/pax)" != "$P" ] && MIRPAX=/usr/bin/pax

# the source tree, made the same way for each copy that is needed
long=$(awk 'BEGIN { while (i++ < 120) printf "x" }')
long2=$(awk 'BEGIN { while (i++ < 99) printf "y" }')
if [ -r /dev/urandom ]; then
    head -c 70000 /dev/urandom > random
else
    LC_ALL=C awk 'BEGIN { srand(1); for (i = 0; i < 70000; i++) printf "%c", 1 + int(rand() * 255) }' > random
fi
mksrc() { # dir [without-bigid]
    mkdir -p "$1/d/e" "$1/empty"
    echo hello > "$1/a"
    cp random "$1/d/random"
    ln "$1/a" "$1/d/hard"
    ln -s ../a "$1/d/sym"
    ln -s nowhere "$1/dangling"
    mkfifo "$1/fifo"
    mkdir -p "$1/$long"; echo long > "$1/$long/$long2"
    printf 'utf' > "$1/$(printf 'na\303\257ve \342\202\254')"
    echo s > "$1/with space"; chmod 751 "$1/with space"
    if [ "$(id -u)" = 0 ]; then
        mknod "$1/chr" c 1 3; mknod "$1/blk" b 7 200
        touch "$1/owned"; chown 1234:5678 "$1/owned"
        [ -n "$2" ] || { touch "$1/bigid"; chown 3000000:3000000 "$1/bigid"; }
    fi
    find "$1" -exec "$LTOUCH" -d '2015-06-01 12:00:00' {} +
    touch -d '2016-01-01T00:00:00.123456789' "$1/a"
    chmod 2775 "$1/d"
}
mksrc src
# odc cannot hold a user ID above 262143: that file is not archived
mksrc srcc nobigid

echo "== archives written by pax, read by others"
for fmt in pax ustar cpio; do
    (cd src && "$P" -w -x $fmt -f ../p.$fmt . 2>/dev/null)
    case $fmt in ustar) continue;; esac   # ustar cannot hold the long names: below
    S=-s; [ $fmt = pax ] && S=
    ref=src; [ $fmt = cpio ] && ref=srcc
    if [ $fmt != cpio ] && have tar; then rm -rf o; mkdir o; (cd o && tar xpf ../p.$fmt); check "$fmt -> GNU tar" "$S" src o; fi
    if have bsdtar; then rm -rf o; mkdir o; (cd o && bsdtar xpf ../p.$fmt); check "$fmt -> bsdtar" "$S" $ref o; fi
    if [ $fmt = cpio ] && have cpio; then rm -rf o; mkdir o; (cd o && cpio -idmu --quiet < ../p.$fmt 2>/dev/null)
        # GNU cpio sets directory times before filling them in: the files only
        (cd srcc && find . ! -type d | sort) > list1; (cd o && find . ! -type d | sort) > list2
        if cmp -s list1 list2; then ok "$fmt -> GNU cpio (files)"; else bad "$fmt -> GNU cpio (files)"; diff list1 list2 | head -n 5; fi
    fi
    if [ -n "$MIRPAX" ]; then rm -rf o; mkdir o; (cd o && $MIRPAX -r -pe -f ../p.$fmt 2>/dev/null); check "$fmt -> MirBSD pax" -s $ref o; fi
done
# ustar: a tree without the long names
rm -rf src2; mkdir src2; (cd src && "$P" -rw -pe -s ",^\./$long.*,," . ../src2) 2>/dev/null
rm -f src2/bigid
(cd src2 && "$P" -w -x ustar -f ../p.ustar .)
if have tar; then rm -rf o; mkdir o; (cd o && tar xpf ../p.ustar); check "ustar -> GNU tar" -s src2 o; fi
if have bsdtar; then rm -rf o; mkdir o; (cd o && bsdtar xpf ../p.ustar); check "ustar -> bsdtar" -s src2 o; fi
if [ -n "$MIRPAX" ]; then rm -rf o; mkdir o; (cd o && $MIRPAX -r -pe -f ../p.ustar); check "ustar -> MirBSD pax" -s src2 o; fi
if have tar; then
    if tar --compare -f p.pax -C src >/dev/null 2>&1; then ok "pax: GNU tar --compare"; else bad "pax: GNU tar --compare"; tar --compare -f p.pax -C src 2>&1 | head -n 3; fi
fi

echo "== archives written by others, read by pax"
mk() { # name command...
    _n=$1; shift
    (cd src && "$@" > "../ar.$_n" 2>/dev/null) && echo "$_n"
}
list=
if have tar; then
    for f in gnu oldgnu posix; do list="$list $(mk gnutar-$f tar cf - --format=$f .)"; done
    list="$list $(mk gnutar-ustar tar cf - --format=ustar ./a ./d ./empty ./fifo ./with\ space)"
    list="$list $(mk gnutar-v7 tar cf - --format=v7 ./a ./d ./empty ./with\ space)"
fi
if have bsdtar; then
    for f in pax ustar newc odc; do
        if [ $f = ustar ]; then list="$list $(mk bsdtar-$f bsdtar cf - --format=$f ./a ./d ./empty ./fifo)"
        else list="$list $(mk bsdtar-$f bsdtar cf - --format=$f .)"; fi
    done
fi
if have cpio; then
    list="$list $(mk cpio-odc sh -c 'find . | cpio -o -H odc --quiet')"
    list="$list $(mk cpio-newc sh -c 'find . | cpio -o -H newc --quiet')"
    list="$list $(mk cpio-crc sh -c 'find . | cpio -o -H crc --quiet')"
fi
if [ -n "$MIRPAX" ]; then
    list="$list $(mk mirpax-cpio sh -c "$MIRPAX -w -x cpio -s ',^./bigid\$,,' .")"
    list="$list $(mk mirpax-sv4cpio $MIRPAX -w -x sv4cpio .)"
    list="$list $(mk mirpax-ustar $MIRPAX -w -x ustar ./a ./d ./empty ./fifo)"
fi
for n in $list; do
    rm -rf o; mkdir o
    if ! (cd o && "$P" -r -pe -f "../ar.$n"); then bad "$n -> pax (exit status)"; continue; fi
    S=-s; case $n in *posix|*bsdtar-pax) S=;; esac
    case $n in
    *ustar|*v7) # partial trees
        rm -rf ref; mkdir ref; (cd src && "$P" -rw -pe ./a ./d ./empty ./fifo ./with\ space ../ref 2>/dev/null)
        case $n in *v7) rm -f ref/fifo;; esac
        case $n in bsdtar-ustar|mirpax-ustar) rm -f "ref/with space";; esac
        check "$n -> pax" -s ref o;;
    mirpax-cpio) check "$n -> pax" -s srcc o;;
    *) check "$n -> pax" "$S" src o;;
    esac
    if [ "$("$P" -f "ar.$n" | sort)" = "$( (bsdtar tf "ar.$n" 2>/dev/null || tar tf "ar.$n") | sed 's,/$,,' | sort)" ]; then ok "$n: listing"; else bad "$n: listing"; fi
done

echo "== appending to archives written by others"
if have tar; then
    (cd src && tar cf ../app.tar --format=ustar ./a)
    echo appended > src/new; touch -d '2015-06-01T00:00:00' src/new
    (cd src && "$P" -w -a -f ../app.tar ./new)
    if [ "$(tar tf app.tar | tr '\n' ' ')" = "./a ./new " ]; then ok "append to GNU tar archive"; else bad "append to GNU tar archive"; tar tf app.tar; fi
    rm src/new
fi
if have cpio; then
    (cd src && echo ./a | cpio -o -H odc --quiet > ../app.cpio)
    (cd src && "$P" -w -a -x cpio -f ../app.cpio ./with\ space)
    if [ "$(cpio -it --quiet < app.cpio | tr '\n' ' ')" = "a ./with space " ] &&
       [ "$("$P" -f app.cpio | tr "\n" " ")" = "a ./with space " ]; then ok "append to GNU cpio archive"; else bad "append to GNU cpio archive"; cpio -it < app.cpio; fi
fi

echo "== copy mode"
rm -rf c1; mkdir c1; "$P" -rw -pe src c1; check "pax -rw -pe: a copy of the source" "" src c1/src

echo "$pass passed, $fail failed"
[ $fail = 0 ]
