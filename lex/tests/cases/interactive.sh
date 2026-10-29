# the scanner reads only as far as it must: with a terminal or a pipe it
# acts on a line before the next one is typed
cat > g.l <<'L'
%{
#include <stdio.h>
%}
%%
[a-z]+\n    { printf("word %.*s\n", yyleng - 1, yytext); fflush(stdout); }
.|\n        ;
L
$LEX g.l && $CC -o g lex.yy.c -L"$LIBDIR" -ll || exit 1
python3 - <<'P'
import subprocess, time
p = subprocess.Popen(['./g'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
for w in [b'one\n', b'two\n']:
    p.stdin.write(w); p.stdin.flush()
    print(p.stdout.readline().decode().strip())
p.stdin.close()
print('exit', p.wait())
P
