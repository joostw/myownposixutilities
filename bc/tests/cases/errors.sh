# diagnostics: syntax and runtime errors name the file and line, execution
# goes on with the next line, and the exit status is 1 after an error
t() { printf '%b' "$1" | $BC 2>&1; echo "  status $?"; }
t '1 +\n2\n'
t '1/0\n3\n'
t '2 % 0\n'
t '0 ^ -1\n'
t 'sqrt(-2)\n'
t 'f(1)\n'
t 'define f(x) { return (x) }\nf(1, 2)\nf()\n'
t 'define f(x[]) { return (x[0]) }\nf(1)\n'
t 'define f(x) { return (x) }\nf(a[])\n'
t 'a[-1] = 1\n'
t 'a[16777215] = 1\n'
t 'break\n'
t 'continue\n'
t 'return (1)\n'
t 'define f() { break }\n'
t 'define f(a, a) { return (1) }\n'
t '"unterminated\n'
t '/* unterminated\n'
t '@\n'
t 'G\n'
t '2 ^ 1.5\n'
t '2 ^ 10000000000\n'
t 'x = 3; y = x / 0; z = 4\nx; y; z\n'
echo 'a = 2' > ok.bc
$BC ok.bc nosuch.bc < /dev/null 2>&1; echo "  status $?"
$BC -x 2>&1; echo "  status $?"
