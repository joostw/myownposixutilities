"""Random differential tests of find, compared with GNU find (as "find" in
PATH): random trees (directories, files of a few sizes and modes,
symbolic links, FIFOs) and random expressions, with -H, -L or neither.

usage: rfind.py FIND SEED N

The sorted outputs and whether the exit status is zero must agree.  With
-L, an entry that GNU find reports as a directory loop and leaves out is
allowed to be listed (this find lists it, and reports the loop without
entering it).  GNU find's expression optimizer can move -name before
-print ("-print ... -name x -o ..."), which changes its output; run GNU
find with -O0 to compare such cases.
"""
import random, subprocess, os, sys, shutil, tempfile
if len(sys.argv) != 4:
    print(__doc__); sys.exit(2)
F=os.path.abspath(sys.argv[1])
seed=int(sys.argv[2]); N=int(sys.argv[3])
rnd=random.Random(seed)
W=tempfile.mkdtemp(prefix='rfind.')
def tree():
    shutil.rmtree(W, ignore_errors=True); os.makedirs(W)
    dirs=['t']; os.makedirs(W+'/t')
    for i in range(rnd.randint(3,25)):
        d=rnd.choice(dirs); nm=rnd.choice(['a','b','c','ab','x.c','.h','Ab','dd'])+str(i%3)
        p=d+'/'+nm
        k=rnd.random()
        if os.path.lexists(W+'/'+p) or not os.path.isdir(W+'/'+d) or os.path.islink(W+'/'+d): continue
        if k<0.3: os.makedirs(W+'/'+p, exist_ok=True); dirs.append(p)
        elif os.path.exists(W+'/'+p): continue
        elif k<0.8:
            open(W+'/'+p,'w').write('x'*rnd.choice([0,1,511,512,513,2000]))
            os.chmod(W+'/'+p, rnd.choice([0o644,0o755,0o600,0o4755,0o640]))
        elif k<0.9: os.symlink(rnd.choice(['a0','../t','nowhere','.']), W+'/'+p)
        else: os.mkfifo(W+'/'+p)
prims=['-type f','-type d','-type l','-type p','-name a*','-name *[0-9]','-iname A*','-path t/a*','-path */b*','-perm 644','-perm -100','-perm -u=x','-perm g=r,u=rw','-links 1','-links +1','-size 0','-size +1','-size -2','-size 512c','-size +512c','-prune','-print','-depth','-mtime 0','-newer t','-nouser','-xdev']
def expr(d=0):
    k=rnd.random()
    if d>2 or k<0.4: return [rnd.choice(prims)]
    if k<0.55: return ['!']+expr(d+1)
    if k<0.7: return ['(']+expr(d+1)+[')']
    if k<0.85: return expr(d+1)+['-o']+expr(d+1)
    return expr(d+1)+expr(d+1)
bad=0
for i in range(N):
    if i%10==0: tree()
    e=' '.join(expr()).split()
    # split two-word primaries back
    args=[]
    for tok in ' '.join(expr()).split(' '): args.append(tok)
    opt=rnd.choice([[],['-H'],['-L']])
    cmd=opt+['t']+args
    r=subprocess.run([F]+cmd,cwd=W,capture_output=True)
    g=subprocess.run(['find','-O0']+cmd,cwd=W,capture_output=True)
    rs=sorted(r.stdout.split(b'\n')); gs=sorted(g.stdout.split(b'\n'))
    if '-depth' not in cmd and '-prune' not in cmd: pass
    diff=set(rs)^set(gs)
    loops=[l for l in diff if b"'"+l+b"'" in g.stderr.replace('\u2018'.encode(),b"'").replace('\u2019'.encode(),b"'")]
    if (diff and set(loops)!=diff) or (r.returncode!=0)!=(g.returncode!=0):
        bad+=1
        if bad<=6: print('==',cmd,r.returncode,g.returncode); print(set(rs)^set(gs)); print(g.stderr[:200])
shutil.rmtree(W, ignore_errors=True)
print('%d passed, %d failed' % (N - bad, bad))
sys.exit(1 if bad else 0)
