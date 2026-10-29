. "$LIB"
# pax archives with chosen extended header records: mkpax OUT 'k=v;k=v' NAME ...
# (one file per NAME, with the records given before it; \xNN in values;
# "g:" before the records makes a global header instead)
mkpax() {
    python3 - "$@" <<'PY'
import sys
out = sys.argv[1]; d = b''
def hdr(name, size, flag):
    h = bytearray(512)
    h[:len(name)] = name[:100]
    h[100:108] = b'0000644\0'; h[108:116] = b'0000000\0'; h[116:124] = b'0000000\0'
    h[124:136] = b'%011o\0' % size; h[136:148] = b'00000000000\0'
    h[156:157] = flag; h[257:263] = b'ustar\0'; h[263:265] = b'00'
    h[148:156] = b'        '
    h[148:156] = b'%06o\0 ' % sum(h)
    return bytes(h)
def rec(k, v):
    body = k + b'=' + v + b'\n'
    n = len(body) + 2
    while len(b'%d ' % n + body) != n:
        n = len(b'%d ' % n + body)
    return b'%d ' % n + body
args = sys.argv[2:]
for i in range(0, len(args), 2):
    recs, name = args[i], args[i + 1].encode()
    flag = b'x'
    if recs.startswith('g:'):
        flag, recs = b'g', recs[2:]
    r = b''
    for kv in filter(None, recs.split(';')):
        k, v = kv.split('=', 1)
        r += rec(k.encode(), v.encode().decode('unicode_escape').encode('latin-1'))
    if r:
        d += hdr(b'PaxHeaders/x', len(r), flag) + r + bytes(-len(r) % 512)
    if name != b'-':
        d += hdr(name, 2, b'0') + b'x\n' + bytes(510)
d += bytes(1024)
open(out, 'wb').write(d)
PY
}
show() { ls "$@" 2>/dev/null | od -An -c | tr -s ' '; }
U=${U8:-}     # a UTF-8 locale, from the runner
[ -n "$U" ] || { echo SKIP; exit 0; }

echo "-- writing in a UTF-8 locale: UTF-8 names as they are, others BINARY"
mkdir w; python3 -c "open(b'w/caf\xc3\xa9','w'); open(b'w/caf\xe9','w')"
LC_ALL=$U $PAX -w -x pax -f w.pax w; hdrs w.pax | grep -E 'hdrcharset|path='
echo "-- writing in the C locale: bytes outside ASCII are BINARY"
LC_ALL=C $PAX -w -x pax -f wc.pax w; hdrs wc.pax | grep -E 'hdrcharset|path='

echo "-- -o hdrcharset:=BINARY: the names as they are"
LC_ALL=$U $PAX -w -x pax -o hdrcharset:=BINARY -f wb.pax w/caf*; hdrs wb.pax | grep -E 'hdrcharset|path=' | sort | uniq -c

mkpax utf8.pax 'path=caf\xc3\xa9' x
mkpax raw.pax 'path=caf\xe9' x
mkpax bin.pax 'hdrcharset=BINARY;path=caf\xe9' x
mkpax glob.pax 'g:hdrcharset=BINARY' - 'path=a\xe9' x 'path=b\xe9' x
mkpax nul.pax 'path=a\x00b' x
mkpax long.pax "path=$(awk 'BEGIN { while (i++ < 300) printf "n" }')" x
mkpax link.pax 'linkpath=t\xe9' y
mkpax iso.pax 'hdrcharset=ISO-IR 10646 2000 UTF-8;path=caf\xc3\xa9' x
mkpax other.pax 'hdrcharset=ISO-IR 8859 1 1998;path=caf\xe9' x
mkpax ok.pax 'path=plain' x

for loc in $U C; do
  if [ "$loc" = C ]; then echo "== reading in C"; else echo "== reading in the UTF-8 locale"; fi
  for a in utf8 raw bin glob nul long iso other ok; do
    rm -rf o; mkdir o; (cd o && { LC_ALL=$loc $PAX -r -f ../$a.pax 2>&1; echo $? > ../rc; } | sed 's/^[^:]*pax: /pax: /'; echo "$a rc=$(cat ../rc)"); show o
  done
done

echo "== the actions, in the C locale, for the UTF-8 name"
for act in bypass binary UTF-8 write; do
  rm -rf o; mkdir o; (cd o && LC_ALL=C $PAX -r -o invalid=$act -f ../utf8.pax 2>&1; echo "$act rc=$?"); show o
done
echo "== the actions in a UTF-8 locale, for the name that is not UTF-8"
for act in bypass binary UTF-8 write; do
  rm -rf o; mkdir o; (cd o && LC_ALL=$U $PAX -r -o invalid=$act -f ../raw.pax 2>&1; echo "$act rc=$?"); show o
done
echo "== a name with a NUL, -o invalid=binary"
rm -rf o; mkdir o; (cd o && LC_ALL=$U $PAX -r -o invalid=binary -f ../nul.pax; echo "rc=$?"); show o
echo "== a name too long, -o invalid=write: tried, and the system refuses"
rm -rf o; mkdir o; (cd o && { LC_ALL=$U $PAX -r -o invalid=write -f ../long.pax 2>&1; echo $? > ../rc; } | sed 's/^[^:]*pax: [n]*/pax: N.../'; echo "rc=$(cat ../rc)"); show o
echo "== a symbolic link whose target cannot be translated"
mkpax sym.pax 'linkpath=t\xe9' -
python3 - <<'PY'
# turn the member of link.pax into a symbolic link
d = bytearray(open('link.pax', 'rb').read())
i = 1024 if d[156:157] == b'x' else 0
h = d[i:i+512]; h[156:157] = b'2'; h[124:136] = b'%011o\0' % 0
h[148:156] = b'        '; h[148:156] = b'%06o\0 ' % sum(h)
d[i:i+512] = h; d = d[:i+512] + bytes(1024)
open('link.pax', 'wb').write(bytes(d))
PY
rm -rf o; mkdir o; (cd o && LC_ALL=$U $PAX -r -f ../link.pax 2>&1; echo "rc=$?"; LC_ALL=$U $PAX -r -o invalid=write -f ../link.pax 2>&1; echo "write rc=$?"); ls -l o | sed -n 's/.* \(y -> .*\)/\1/p' | od -An -c | tr -s ' '

echo "== list mode lists every file"
for a in utf8 raw nul; do LC_ALL=C $PAX -f $a.pax | od -An -c | tr -s ' '; echo "$a rc=$?"; done

echo "== -o invalid=rename asks, as -i does"
python3 - "$PAX" <<'PY'
import os, pty, select, sys, time
pax = sys.argv[1]
os.mkdir('r')
pid, fd = pty.fork()
if pid == 0:
    os.chdir('r'); os.environ['LC_ALL'] = 'C'
    os.execv(pax, [pax, '-r', '-o', 'invalid=rename', '-f', '../utf8.pax'])
out = b''
def drain(t=0.4):
    global out
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try: out += os.read(fd, 1000)
            except OSError: return
drain(); os.write(fd, b'cafe\n'); drain()
_, st = os.waitpid(pid, 0)
print(repr(out.replace(b'\r', b'')))
print('rc', os.waitstatus_to_exitcode(st), sorted(os.listdir('r')))
PY
