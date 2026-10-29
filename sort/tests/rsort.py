"""Random differential tests of sort, compared with GNU sort (as "sort" in
PATH): random lines of fields, random options and -k keys.

usage: rsort.py SORT SEED N [LOCALE [ascii]]

LOCALE UTF8 is a UTF-8 locale (as testlib finds one); without one the run
is skipped.

Also: -c accepts GNU sort's output, -C agrees with GNU sort -C, and -m of
two sorted halves agrees with GNU sort -m.  Combinations the standard
leaves undefined (-d or -i with -n, -d with -i) are skipped.  GNU sort
counts key positions in bytes and folds case byte by byte, so in a UTF-8
locale give "ascii" to keep the input ASCII.
"""
import random, subprocess, sys, os, tempfile, shutil
if len(sys.argv) < 4:
    print(__doc__); sys.exit(2)
S=os.path.abspath(sys.argv[1])
W=tempfile.mkdtemp(prefix='rsort.')
seed=int(sys.argv[2]); N=int(sys.argv[3]); loc=sys.argv[4] if len(sys.argv)>4 else 'C'
if loc == 'UTF8':
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'testlib'))
    import testlib
    loc = testlib.utf8_locale()
    if not loc:
        print('no UTF-8 locale: skipped'); print('0 passed, 0 failed'); sys.exit(0)

rnd=random.Random(seed)
ascii = len(sys.argv)>5
words=['a','B','b','10','9','-3','0','-0','00','1.5','1.50',' x','é','É','e','a-b','x:y','','  ','Z','zz','.5','-.5','3e2','\tq']
if ascii: words=[w for w in words if w.isascii()]
def line():
    sep=rnd.choice([' ','  ',':','\t',' '])
    return sep.join(rnd.choice(words) for _ in range(rnd.randint(0,4)))
def keydef():
    f=rnd.randint(1,4); s=str(f)
    if rnd.random()<0.3: s+='.'+str(rnd.randint(1,3))
    if rnd.random()<0.4: s+=''.join(rnd.sample('bdfinr',rnd.randint(1,2)))
    if rnd.random()<0.6:
        e=rnd.randint(f,5); s+=','+str(e)
        if rnd.random()<0.3: s+='.'+str(rnd.randint(0,3))
        if rnd.random()<0.3: s+=rnd.choice('bnr')
    return s
bad=0
env=dict(os.environ, LC_ALL=loc)
for i in range(N):
    lines=[line() for _ in range(rnd.randint(0,30))]
    data=''.join(l+'\n' for l in lines)
    opts=[]
    if rnd.random()<0.5: opts+=['-'+''.join(rnd.sample('bdfinru',rnd.randint(1,2)))]
    if rnd.random()<0.4: opts+=['-t',rnd.choice([':',' ','\t'])]
    for _ in range(rnd.choice([0,0,1,2,3])): opts+=['-k',keydef()]
    # -d/-i with -n: undefined; drop those
    flat=''.join(o for o in opts if o.startswith('-') or True)
    r=subprocess.run([S]+opts,input=data.encode(),capture_output=True,env=env)
    g=subprocess.run(['sort']+opts,input=data.encode(),capture_output=True,env=env)
    fl=''.join(o for o in opts if o not in ('-t',':',' ','\t'))
    if ('d' in fl and 'i' in fl) or ('n' in fl and ('d' in fl or 'i' in fl)): continue
    if r.stdout!=g.stdout or r.returncode!=g.returncode:
        bad+=1
        if bad<=5:
            print('== opts',opts,'rc',r.returncode,g.returncode); print(repr(data))
            print('ours',r.stdout.decode(errors='replace').splitlines()); print('gnu ',g.stdout.decode(errors='replace').splitlines()); print(r.stderr.decode()[:200])
    # -c on GNU-sorted output, -C on the unsorted
    if g.returncode==0:
        c=subprocess.run([S,'-c']+opts,input=g.stdout,capture_output=True,env=env)
        if c.returncode!=0: bad+=1; print('CHECK fails on sorted', opts)
        c1=subprocess.run([S,'-C']+opts,input=data.encode(),capture_output=True,env=env)
        c2=subprocess.run(['sort','-C']+opts,input=data.encode(),capture_output=True,env=env)
        if c1.returncode!=c2.returncode: bad+=1; print('-C differs', opts, c1.returncode, c2.returncode)
        # merge: split sorted output in two sorted halves
        ls=g.stdout.split(b'\n')[:-1]
        a=b''.join(x+b'\n' for x in ls[0::2]); b=b''.join(x+b'\n' for x in ls[1::2])
        open(W+'/m1','wb').write(a); open(W+'/m2','wb').write(b)
        if '-u' not in ''.join(opts) and 'u' not in (opts[0] if opts and not opts[0].startswith('-t') and not opts[0].startswith('-k') else ''):
            m=subprocess.run([S,'-m']+opts+['m1','m2'],cwd=W,capture_output=True,env=env)
            gm=subprocess.run(['sort','-m']+opts+['m1','m2'],cwd=W,capture_output=True,env=env)
            if m.stdout!=gm.stdout: bad+=1; print('MERGE differs',opts)
shutil.rmtree(W, ignore_errors=True)
print(loc, N, 'cases,', bad, 'differ')
print('%d passed, %d failed' % (N - bad, bad))
sys.exit(1 if bad else 0)
