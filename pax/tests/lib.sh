# helpers for the regression cases
T0=200001010000     # a fixed time for touch -t
mktree() {           # a small tree with every portable file type
    mkdir -p src/dir/sub src/empty
    echo one > src/one
    printf 'two\nlines\n' > src/dir/two
    awk 'BEGIN { while (i++ < 3000) printf "z" }' > src/dir/sub/big
    ln src/one src/dir/hard
    ln -s ../one src/dir/sym
    mkfifo src/fifo
    chmod 640 src/dir/two
    find src -exec "$LTOUCH" -t $T0 {} +
}
# exact file status, and file times without following symbolic links, which
# POSIX stat and touch lack (../../testlib/fstat.c and ltouch.c, from the runner)
fstat() { "$FSTAT" "$@"; }
treecmp() { python3 "$TREECMP" "$@" && echo "trees equal"; }
hdrs() {             # list the tar headers of an archive: typeflag, name, size, mode
    python3 - "$1" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read(); i = 0
while i + 512 <= len(d):
    h = d[i:i+512]
    if h == bytes(512):
        print('zero block at', i); break
    name = h[:100].rstrip(b'\0'); pre = h[345:500].rstrip(b'\0')
    sz = int(h[124:136].strip(b'\0 ') or b'0', 8)
    full = (pre + b'/' + name) if pre else name
    print(h[156:157].decode() or '\\0', full.decode(errors='replace'), sz, h[100:108].rstrip(b'\0 ').decode())
    if h[156:157] in (b'x', b'g'):
        for rec in d[i+512:i+512+sz].decode(errors='replace').splitlines():
            print('   ', rec)
    i += 512 + (sz + 511) // 512 * 512 if h[156:157] not in (b'2',b'3',b'4',b'5',b'6') else 512
print('archive size', len(d))
PY
}
