"""Random differential tests of diff: pairs of files of a few kinds of
lines, compared with GNU diff (as "diff" in PATH), with and without -b.

usage: rdiff.py DIFF SEED N

For each pair: the edit script must be no longer than GNU diff's (both
should be minimal); the default and -u output, given to patch, and the -e
output with "w" appended, given to ed, must make file2 of file1.  The
output itself may differ from GNU diff's where several minimal scripts
exist; how often it is the same is reported.  Exit status 1 if a check
failed.
"""
import random, subprocess, os, sys, tempfile, shutil
if len(sys.argv) != 4:
    print(__doc__); sys.exit(2)
D=os.path.abspath(sys.argv[1])
W=tempfile.mkdtemp(prefix='rdiff.')
seed=int(sys.argv[2]); N=int(sys.argv[3])
rnd=random.Random(seed)
def gen():
    alpha=[rnd.choice(['a','b','c','d','.','','x y','x  y ',' ','e\tf']) for _ in range(rnd.randint(1,6))]
    n=rnd.randint(0,40)
    lines=[rnd.choice(alpha) for _ in range(n)]
    return lines
def mutate(lines):
    l=list(lines)
    for _ in range(rnd.randint(0,8)):
        op=rnd.random()
        if op<0.3 and l: del l[rnd.randrange(len(l))]
        elif op<0.6: l.insert(rnd.randint(0,len(l)), rnd.choice(['a','b','z','.','q r','']))
        elif l: l[rnd.randrange(len(l))]=rnd.choice(['a','Z','.','x y',''])
    return l
def text(lines, nl=True):
    s='\n'.join(lines)
    if lines and nl: s+='\n'
    return s
def count(out):
    return sum(1 for l in out.splitlines() if l.startswith(('< ','> ')) or l in ('<','>'))
stats=dict(same=0,cases=0,longer=0,ed_bad=0,patch_bad=0,upatch_bad=0,status=0)
for i in range(N):
    A=gen(); B=mutate(A) if rnd.random()<0.8 else gen()
    nla = rnd.random()<0.9; nlb = rnd.random()<0.9
    open(W+'/a','w').write(text(A,nla)); open(W+'/b','w').write(text(B,nlb))
    for opts in ([],['-b']):
        r=subprocess.run([D]+opts+['a','b'],cwd=W,capture_output=True)
        g=subprocess.run(['diff']+opts+['a','b'],cwd=W,capture_output=True)
        stats['cases']+=1
        if r.returncode!=g.returncode: stats['status']+=1; print('STATUS',seed,i,opts,r.returncode,g.returncode)
        if r.stdout==g.stdout: stats['same']+=1
        if count(r.stdout.decode())>count(g.stdout.decode()):
            stats['longer']+=1; print('LONGER',seed,i,opts)
        if not opts:
            # normal format via patch
            if r.returncode==1:
                open(W+'/p','wb').write(r.stdout)
                subprocess.run(['cp','a','pa'],cwd=W)
                subprocess.run(['patch','-s','pa','p'],cwd=W,capture_output=True)
                if open(W+'/pa','rb').read()!=open(W+'/b','rb').read(): stats['patch_bad']+=1; print('PATCH',seed,i)
                u=subprocess.run([D,'-u','a','b'],cwd=W,capture_output=True)
                open(W+'/pu','wb').write(u.stdout)
                subprocess.run(['cp','a','ua'],cwd=W)
                subprocess.run(['patch','-s','ua','pu'],cwd=W,capture_output=True)
                if open(W+'/ua','rb').read()!=open(W+'/b','rb').read(): stats['upatch_bad']+=1; print('UPATCH',seed,i)
                if nla and nlb:
                    e=subprocess.run([D,'-e','a','b'],cwd=W,capture_output=True)
                    subprocess.run(['cp','a','ea'],cwd=W)
                    subprocess.run(['ed','-s','ea'],cwd=W,input=e.stdout+b'w\nq\n',capture_output=True)
                    if open(W+'/ea','rb').read()!=open(W+'/b','rb').read(): stats['ed_bad']+=1; print('ED',seed,i)
shutil.rmtree(W, ignore_errors=True)
print(stats)
fails = stats['longer'] + stats['ed_bad'] + stats['patch_bad'] + stats['upatch_bad'] + stats['status']
print('%d passed, %d failed' % (stats['cases'] - fails, fails))
sys.exit(1 if fails else 0)
