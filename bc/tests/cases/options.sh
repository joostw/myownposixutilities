# -l defines the library and sets scale to 20; files are read before
# the standard input; quit stops at once, even in a function definition
printf 'scale\n' | $BC
printf 'scale\n' | $BC -l
printf 'x = 7\n' > a.bc
printf 'x * 2\n' > b.bc
printf 'x + 1\n' | $BC a.bc b.bc
printf 'quit\n' > q.bc
printf '1\n' | $BC q.bc; echo "status $?"
printf 'define f() {\n quit\n}\n9\n' | $BC; echo "status $?"
printf '5\nhalt\n6\n' | $BC; echo "status $?"
printf 'x = 3\n' | $BC -- a.bc; echo "status $?"
