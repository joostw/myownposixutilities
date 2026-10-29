"""compressdiff.py - compare compress with gzip and Python's zlib.

    python3 tests/compressdiff.py ./compress

Inputs of many kinds and sizes (random bytes, runs, repeated strings,
text) go each way: what compress writes in both formats must be expanded
by gzip -d (which reads .Z too) and, for gzip, by Python's gzip module,
and what gzip and Python write at every level must be expanded by
compress -d.  Damaged input must not crash it.  Needs gzip in PATH.
"""

import gzip
import os
import random
import shutil
import subprocess
import sys


def run(cmd, data):
    return subprocess.run(cmd, input=data, capture_output=True)


def inputs(rng):
    yield b""
    yield b"a"
    yield b"hello\n"
    yield bytes(range(256)) * 40
    yield b"x" * 100000
    yield rng.randbytes(70000)
    words = [rng.randbytes(rng.randint(1, 9)).hex() for _ in range(500)]
    yield " ".join(rng.choice(words) for _ in range(60000)).encode()
    for _ in range(60):
        parts = []
        for _ in range(rng.randint(0, 40)):
            k = rng.random()
            if k < 0.3:
                parts.append(rng.randbytes(rng.randint(0, 3000)))
            elif k < 0.6:
                parts.append(bytes([rng.randint(0, 3)]) * rng.randint(1, 5000))
            else:
                parts.append(rng.randbytes(rng.randint(1, 40)) * rng.randint(1, 300))
        yield b"".join(parts)


def main():
    prog = os.path.abspath(sys.argv[1])
    if not shutil.which("gzip"):
        print("compressdiff: gzip is needed")
        sys.exit(2)
    rng = random.Random(1952)
    passed = failed = 0

    def check(name, ok):
        nonlocal passed, failed
        if ok:
            passed += 1
        else:
            failed += 1
            print("FAIL", name)

    for n, data in enumerate(inputs(rng)):
        bits = rng.randint(9, 16)
        z = run([prog, "-c", "-f", "-b", str(bits)], data).stdout
        check(f"{n}: .Z ({bits} bits) expanded by gzip", run(["gzip", "-dc"], z).stdout == data)
        check(f"{n}: .Z ({bits} bits) expanded by itself", run([prog, "-dc"], z).stdout == data)
        level = rng.randint(1, 9)
        g = run([prog, "-c", "-f", "-g", "-b", str(level)], data).stdout
        check(f"{n}: .gz (level {level}) expanded by gzip", run(["gzip", "-dc"], g).stdout == data)
        try:
            ok = gzip.decompress(g) == data
        except Exception:
            ok = False
        check(f"{n}: .gz (level {level}) expanded by Python", ok)
        g2 = run(["gzip", f"-{level}", "-c"], data).stdout
        check(f"{n}: gzip -{level} expanded", run([prog, "-dc"], g2).stdout == data)
        p = gzip.compress(data, compresslevel=rng.randint(0, 9))
        check(f"{n}: Python's gzip expanded", run([prog, "-dc"], p).stdout == data)
        for src, what in ((z, ".Z"), (g, ".gz")):
            if len(src) > 12:
                d = bytearray(src)
                for _ in range(3):
                    d[rng.randrange(2, len(d))] = rng.randrange(256)
                r = run([prog, "-dc"], bytes(d))
                check(f"{n}: damaged {what}: no crash", r.returncode >= 0)
    print(f"compressdiff: {passed} passed, {failed} failed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
