"""Cases for difftest.py: a bc program (and options and files) whose
standard output must be the same as GNU bc's."""

CASES = [
    dict(name='spec-pi', prog='scale = 10\n"pi equals "\n104348 / 33215\n'),
    dict(name='spec-e', prog='''scale = 20
define e(x){
    auto a, b, c, i, s
    a = 1
    b = 1
    s = 1
    for (i = 1; 1 == 1; i++){
        a = a*x
        b = b*i
        c = a/b
        if (c == 0) {
             return(s)
        }
        s = s+c
    }
}
for (i = 1; i <= 10; ++i) {
    e(i)
}
'''),
    dict(name='arith-scales', prog='''1+2*3-4/5
scale=3; 1/3; 2/3; -2/3; 10/4; 7%3; -7%3; 7%-3; 7.5%2; 0.5*0.25; 1.5*1.5
scale=0; 7/2; -7/2; 7%2; 2.5*2.5; 3.2/1
scale=10; 1.25^3; 2^-3; (-2)^3; (-2)^-3; 0^0; 10^20; .5^10; 1.000^2
scale=2; 3.14159 + 0; 3.14159 * 1; 3.14159 / 1; -3.14159 - 0
'''),
    dict(name='mod-scale', prog='''scale=5; 7%3; 22%7; 1%0.3; -1%0.3
scale=0; 22%7; -22%7; 22%-7; 2.5%1
scale=2; 2.5%1; 10.12%3.4
'''),
    dict(name='power', prog='''2^100; 3^-1; scale=5; 3^-1; 1.1^10; 1.01^100; (-1.5)^3; 2^0; 0^5
scale=0; 1.5^2; 1.5^3; 12.345^2
'''),
    dict(name='sqrt', prog='''sqrt(16); sqrt(2); sqrt(0.25); scale=10; sqrt(2); sqrt(1000000); sqrt(.0001); sqrt(123456789.123)
scale(sqrt(2)); scale=0; scale(sqrt(2.12345)); sqrt(2.12345)
'''),
    dict(name='length-scale', prog='''length(123); length(123.456); length(0.001); length(1.000); length(100)
scale(123); scale(1.000); scale(1/3); scale=4; scale(1/3); scale(2.5*2.5)
'''),
    dict(name='output-bases', prog='''obase=2; 10; 255; -5; 0.5; 0.1
obase=8; 64; 8.5
obase=16; 255; 4095.75; -1; 0.1; 255.255
obase=3; 1/3; scale=5; 1/3
obase=17; 300
obase=25; 1024; 1024.5; .04
obase=125; 1024
obase=1000; 123456789
obase=10; 1/3
'''),
    dict(name='input-bases', prog='''ibase=16; FF; A.8; .C; 1F.F; 10
ibase=A; ibase=2; 1010; 1.1; 111.111
ibase=A; ibase=8; 777; 7.7; 10
ibase=A
A; F; 15
'''),
    dict(name='single-digit-ibase', prog='''ibase=16
obase=A
FF
ibase=A
FF
'''),
    dict(name='long-numbers', prog='''2^500
scale=100; 1/7; -1/7; sqrt(2)
obase=16; 2^300
obase=17; 2^200
'''),
    dict(name='string-column', prog='"pi equals "\n2^300\n"x"\n"\n"\n2^300\n'),
    dict(name='line-length', prog='2^300\n', env={'BC_LINE_LENGTH': '30'}),
    dict(name='line-length-0', prog='2^300\n', env={'BC_LINE_LENGTH': '0'}),
    dict(name='variables-arrays', prog='''a = 5; b[3] = 7; a; b[3]; b[2]; a[3] = 2; a; a[3]
c = 1.50; c; c++; c; ++c; c--; --c; c
d[1.9] = 4; d[1]; scale = 3; e = 1/3; e; e *= 3; e; e += 1; e -= 0.5; e; e /= 2; e; e %= 1; e; e ^= 2; e
'''),
    dict(name='registers', prog='''scale; ibase; obase
scale = 2.9; scale; ibase = 16; ibase; ibase = A; obase = 16; obase; obase = A
scale(scale); x = scale++; x; scale; ++scale
'''),
    dict(name='print-assign', prog='a = 5\n(a = 6)\n(a += 1)\na\na++\n++a\n-a\n'),
    dict(name='functions', prog='''define f(x) {
    return (x * 2)
}
define g(x, y) { return (x + y) }
define h() {
    return
}
define k(x) {
}
f(3); g(2, 3); h(); k(5)
define f(x) { return (x * 3) }
f(3)
define r(n) {
    if (n <= 1) return (1)
    return (n * r(n - 1))
}
r(25)
define fib(n) {
    if (n < 2) return (n)
    return (fib(n - 1) + fib(n - 2))
}
fib(15)
'''),
    dict(name='auto-dynamic-scope', prog='''define f() {
    auto a
    a = 10
    return (g())
}
define g() {
    return (a)
}
a = 1
f(); g(); a
define h(a) {
    a = 99
    return (g())
}
h(5); a
'''),
    dict(name='array-params', prog='''a[0] = 1; a[1] = 2; a[2] = 3
define s(x[], n) {
    auto i, t
    for (i = 0; i < n; i++) t += x[i]
    x[0] = 100
    return (t)
}
s(a[], 3); a[0]
define c(n) {
    auto b[]
    b[n] = n
    return (b[n] + b[n - 1])
}
c(5); b[5]
define t(n, x[]) { return (x[n]) }
t(2, a[])
'''),
    dict(name='constants-in-functions', prog='''define f() { return (10) }
f()
ibase = 16
f()
ibase = A
f()
'''),
    dict(name='control', prog='''for (i = 0; i < 5; i++) i
i = 0
while (i < 3) { i; i = i + 1 }
for (i = 0; i < 10; i++) { if (i == 3) break; i }
if (1 == 1) "yes\n"
if (1 != 1) "no\n"
i = 0; while (1) { if (++i > 4) break; }; i
for (i = 10; i > 0; i = i - 3) i
'''),
    dict(name='relational', prog='''if (2 < 3) 1
if (2 > 3) 2
if (2 <= 2) 3
if (2 >= 3) 4
if (2 == 2.0) 5
if (2 != 2) 6
if (-1 < 0) 7
if (0.001 > 0) 8
'''),
    dict(name='strings', prog='''"hello, world\n"
"a\\nb"
"
"
"multi
line
"
"tab\there\n"
'''),
    dict(name='comments-continuations', prog='''/* a comment */ 1 + /* inline */ 2
/* a
   multi-line comment */
3 + \\
4
12\\
34
1.5\\
5
'''),
    dict(name='semicolons', prog='1; 2;; 3;\n;\n\n4\n'),
    dict(name='quit', prog='1\n2\nquit\n3\n'),
    dict(name='quit-in-definition', prog='1\ndefine f() {\n  quit\n}\n2\n'),
    dict(name='quit-mid-line', prog='1; 2; quit\n'),
    dict(name='no-final-newline', prog='1+1', differs='a last line without a newline is executed; GNU bc reports a syntax error'),
    dict(name='divide-by-zero', prog='1/0\n2\nx = 3; y = x / 0; 4\n5\n0^-1\n6\n'),
    dict(name='errors-recover', prog='1 +\n2\n)\n3\nsqrt(-4)\n4\n'),
    dict(name='math-lib', args=['-l'], prog='''scale
e(0); s(0); c(0); a(0); l(1); j(0,0)
e(1); e(-1); e(10); e(-10); l(2); l(0.5); l(100); s(1); c(1); a(1); a(-1); a(100)
s(10); c(-3); j(2,3.5); j(-1,2); j(3,-2); j(1, 10)
scale=5; e(1.5); s(1); scale
scale=0; e(1); a(1)
scale=50; s(100); e(1); l(10); a(.2); c(1)
'''),
    dict(name='math-pi', args=['-l'], prog='scale=500; 4*a(1)\n'),
    dict(name='math-redefine', args=['-l'], prog='define s(x) { return (x) }\ns(5)\n'),
    dict(name='ext-else-print', prog='''if (1 > 2) 1 else 2
print "a=", 5, "\\n"
print 1/3, "\\n"
x = 0; while (x < 5) { x++; if (x == 2) continue; x }
1 && 0; 1 || 0; !0; !5; 3 > 2 && 2 > 1
'''),
    dict(name='ext-long-names', prog='alpha = 3; beta_2 = 4; alpha * beta_2\ndefine sq(value) { return (value * value) }\nsq(alpha)\n'),
    dict(name='files', files={'one.bc': 'a = 5\ndefine f(x) { return (x + a) }\n', 'two.bc': 'f(1)\n'},
         prog='f(2)\n'),
    dict(name='big-mult', prog='''x = 2^2000 - 1
y = 3^1000 + 7
x * y % 1000000007
x / y
scale = 50; x / y
'''),
    dict(name='big-division', prog='''scale=0; (10^200 + 12345) / (10^100 + 1); (10^200 + 12345) % (10^100 + 1)
scale=20; 123456789012345678901234567890 / 987654321
'''),
    dict(name='digits-beyond-ibase', prog='ibase=2\n3\n3.1\n31\n13\n.3\n3.3\n10.3\nibase=8\n9\n99\n9.9\n'),
    dict(name='math-exact-points', args=['-l'], prog='scale=40; c(0); s(0); e(0); l(1); a(0); x = c(0) - 1; x\nscale=5; c(0); c(10)\n'),
    dict(name='return-forms', prog='define f(n) { if (n < 2) return 1; return n * f(n - 1) }\nf(20)\ndefine h(x) { return (x) + 1 }\nh(2)\n'),
    dict(name='return-empty-parens', prog='define g() { return () }\ng()\n', differs='POSIX allows return (); GNU bc reports a syntax error'),
    dict(name='ibase-after-error', prog='ibase = 2; 1/0; ibase = A\nibase\n'),
]
