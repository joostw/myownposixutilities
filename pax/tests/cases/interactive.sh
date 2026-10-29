. "$LIB"; echo 1 > a; echo 2 > b; echo 3 > c; $PAX -w -f t.tar a b c
python3 - "$PAX" <<'PY'
import os, pty, select, sys, time
pax = sys.argv[1]
os.mkdir('o')
pid, fd = pty.fork()
if pid == 0:
    os.chdir('o'); os.execv(pax, [pax, '-r', '-i', '-f', '../t.tar'])
out = b''
def drain(t=0.4):
    global out
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try: out += os.read(fd, 1000)
            except OSError: return
for reply in (b'renamed\n', b'\n', b'.\n'):
    drain(); os.write(fd, reply)
drain()
_, st = os.waitpid(pid, 0)
print(out.decode().replace('\r', ''))
print('rc', os.waitstatus_to_exitcode(st), sorted(os.listdir('o')))
PY
