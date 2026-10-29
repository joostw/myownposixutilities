# real lexers: a C lexer on C source
for l in "$LEXERS"/*.l; do
    b=$(basename "$l" .l)
    cp "$l" .
    $LEX -v "$b.l" | sed 's/^/  /'
    $CC -O1 -o "$b" lex.yy.c -L"$LIBDIR" -ll || echo "  does not compile"
done
cat > in.c <<'C'
#include <stdio.h>
/* comment */ int main(void) { long x = 0x1fUL + 017 + 3.5e+2f; char *s = "a\"b"; // tail
    return x >= 10 ? 'q' : '\n'; }
C
./c < in.c
