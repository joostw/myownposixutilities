# deep recursion works; endless recursion is an error, not a crash
printf 'define r(n) { if (n == 0) return (0); return (1 + r(n - 1)) }\nr(100000)\n' | $BC
printf 'define r(n) { return (r(n + 1)) }\nr(0)\n5\n' | $BC 2>&1; echo "status $?"
