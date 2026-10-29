#!/usr/bin/env python3
"""Compare two file trees: types, modes, contents, link targets, mtimes, hard links."""
import os, sys, stat

def scan(root, ns=True, atime=False):
    out = {}
    inos = {}
    for dp, dns, fns in os.walk(root):
        for n in dns + fns:
            p = os.path.join(dp, n)
            rel = os.path.relpath(p, root)
            st = os.lstat(p)
            d = {'mode': oct(st.st_mode)}
            if stat.S_ISREG(st.st_mode):
                d['data'] = open(p, 'rb').read()
                if st.st_nlink > 1:
                    inos.setdefault(st.st_ino, []).append(rel)
            if stat.S_ISLNK(st.st_mode):
                d['link'] = os.readlink(p)
            elif not stat.S_ISDIR(st.st_mode) or True:
                d['mtime'] = st.st_mtime_ns if ns else st.st_mtime_ns // 10**9
            if stat.S_ISCHR(st.st_mode) or stat.S_ISBLK(st.st_mode):
                d['rdev'] = st.st_rdev
            out[rel] = d
    groups = sorted(sorted(g) for g in inos.values())
    return out, groups

def main():
    args = sys.argv[1:]
    ns = True
    if args and args[0] == '-s':
        ns = False; args = args[1:]
    a, ga = scan(args[0], ns)
    b, gb = scan(args[1], ns)
    bad = 0
    for k in sorted(set(a) | set(b)):
        if k not in a or k not in b:
            print('only in', args[0] if k in a else args[1], k); bad = 1; continue
        for f in set(a[k]) | set(b[k]):
            if a[k].get(f) != b[k].get(f):
                va, vb = a[k].get(f), b[k].get(f)
                if f == 'data':
                    va, vb = len(va or b''), len(vb or b'')
                print('%s: %s differs: %s vs %s' % (k, f, va, vb)); bad = 1
    if ga != gb:
        print('hard links differ:', ga, gb); bad = 1
    sys.exit(bad)
main()
