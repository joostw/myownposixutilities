"""mkterminfo.py - write a compiled terminfo entry, for the tests of tput
and tabs (the format of term(5); POSIX has no utility that compiles one).

    python3 mkterminfo.py DIR NAMES [-32] CAP...

writes DIR/<first character>/<first name>.  NAMES is "name|...|long name".
A CAP is NAME (a boolean), NAME#NUMBER, or NAME=STRING with the escapes
\\E (ESC), \\n, \\r, \\t, \\\\, \\nnn (octal) and ^X; a NAME that is not a
standard capability is an extended one.  -32 writes 32-bit numbers (the
magic 01036).
"""

import os
import struct
import sys

# the standard capabilities in the order of the format (those of ncurses)
BOOLS = ("bw am xsb xhp xenl eo gn hc km hs in da db mir msgr os eslok xt hz ul xon nxon mc5i "
         "chts nrrmc npc ndscr ccc bce hls xhpa crxm daisy xvpa sam cpix lpix OTbs OTns OTnc "
         "OTMT OTNL OTpt OTxr").split()
NUMS = ("cols it lines lm xmc pb vt wsl nlab lh lw ma wnum colors pairs ncv bufsz spinv spinh "
        "maddr mjump mcs mls npins orc orl orhi orvi cps widcs btns bitwin bitype OTug OTdC "
        "OTdN OTdB OTdT OTkn").split()
# the strings up to those the tests use (the order of ncurses' strnames)
STRS = ("cbt bel cr csr tbc clear el ed hpa cmdch cup cud1 home civis cub1 mrcup cnorm cuf1 "
        "ll cuu1 cvvis dch1 dl1 dsl hd smacs blink bold smcup smdc dim smir invis prot rev "
        "smso smul ech rmacs sgr0 rmcup rmdc rmir rmso rmul flash ff fsl is1 is2 is3 if ich1 "
        "il1 ip kbs ktbc kclr kctab kdch1 kdl1 kcud1 krmir kel ked kf0 kf1 kf10 kf2 kf3 kf4 "
        "kf5 kf6 kf7 kf8 kf9 khome kich1 kil1 kcub1 kll knp kpp kcuf1 kind kri khts kcuu1 rmkx "
        "smkx lf0 lf1 lf10 lf2 lf3 lf4 lf5 lf6 lf7 lf8 lf9 rmm smm nel pad dch dl cud ich indn "
        "il cub cuf rin cuu pfkey pfloc pfx mc0 mc4 mc5 rep rs1 rs2 rs3 rf rc vpa sc ind ri "
        "sgr hts").split()


def unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 3 < len(s) + 1 and s[i + 1:i + 4].isdigit() and len(s[i + 1:i + 4]) == 3:
            v = int(s[i + 1:i + 4], 8)
            out.append(chr(v if v else 0x80))
            i += 4
        elif c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            out.append({"E": "\x1b", "e": "\x1b", "n": "\n", "r": "\r", "t": "\t", "\\": "\\",
                        "s": " ", "0": "\x80"}.get(n, n))
            i += 2
        elif c == "^" and i + 1 < len(s):
            out.append(chr(ord(s[i + 1].upper()) & 0x1F) if s[i + 1] != "?" else "\x7f")
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def main():
    d, names, args = sys.argv[1], sys.argv[2], sys.argv[3:]
    wide = "-32" in args
    args = [a for a in args if a != "-32"]
    bools, nums, strs, xb, xn, xs = {}, {}, {}, {}, {}, {}
    for a in args:
        if "=" in a:
            k, v = a.split("=", 1)
            (strs if k in STRS else xs)[k] = unescape(v).encode("latin-1")
        elif "#" in a:
            k, v = a.split("#", 1)
            (nums if k in NUMS else xn)[k] = int(v)
        else:
            (bools if a in BOOLS else xb)[a] = 1
    nb = max([BOOLS.index(k) + 1 for k in bools] or [0])
    nn = max([NUMS.index(k) + 1 for k in nums] or [0])
    ns = max([STRS.index(k) + 1 for k in strs] or [0])
    nfmt = "<i" if wide else "<h"
    namesb = names.encode() + b"\0"
    table = b""
    offs = []
    for k in STRS[:ns]:
        if k in strs:
            offs.append(len(table))
            table += strs[k] + b"\0"
        else:
            offs.append(-1)
    out = struct.pack("<6h", 0o1036 if wide else 0o432, len(namesb), nb, nn, ns, len(table))
    out += namesb + bytes(bools.get(k, 0) for k in BOOLS[:nb])
    if len(out) % 2:
        out += b"\0"
    out += b"".join(struct.pack(nfmt, nums.get(k, -1)) for k in NUMS[:nn])
    out += b"".join(struct.pack("<h", o) for o in offs) + table
    if xb or xn or xs:
        if len(out) % 2:
            out += b"\0"
        xtab, soffs = b"", []
        for v in xs.values():
            soffs.append(len(xtab))
            xtab += v + b"\0"
        noffs, ntab = [], b""
        for k in list(xb) + list(xn) + list(xs):
            noffs.append(len(ntab))
            ntab += k.encode() + b"\0"
        x = b"".join(bytes([1]) for _ in xb)
        if len(x) % 2:
            x += b"\0"
        x += b"".join(struct.pack(nfmt, v) for v in xn.values())
        x += b"".join(struct.pack("<h", o) for o in soffs + noffs)
        out += struct.pack("<5h", len(xb), len(xn), len(xs), len(xs) + len(xb) + len(xn) + len(xs),
                           len(xtab) + len(ntab)) + x + xtab + ntab
    first = names.split("|")[0]
    os.makedirs(os.path.join(d, first[0]), exist_ok=True)
    with open(os.path.join(d, first[0], first), "wb") as f:
        f.write(out)


if __name__ == "__main__":
    main()
