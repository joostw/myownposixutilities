# -t writes to standard output; -v statistics (to stderr with -t); -n
# silences them; several files are concatenated; - is standard input
printf '%%%%\na\tprintf("A");\n' > one.l
$LEX -t one.l > out.c && test ! -f lex.yy.c && grep -c 'int yylex(void)' out.c
$LEX -v one.l | sed 's/[0-9][0-9]*/N/g'
$LEX -t -v one.l 2>&1 >/dev/null | head -n 1 | sed 's/[0-9][0-9]*/N/g'
$LEX -vn one.l; echo "-vn: $?"
printf '%%{\n#include <stdio.h>\n%%}\n' > defs.l
printf '%%%%\nb\tprintf("B");\n' > rules.l
$LEX defs.l rules.l && $CC -o ab lex.yy.c -L"$LIBDIR" -ll && echo abc | ./ab
cat rules.l | $LEX defs.l - && $CC -o ab2 lex.yy.c -L"$LIBDIR" -ll && echo abc | ./ab2
cat one.l | $LEX && $CC -o a3 lex.yy.c -L"$LIBDIR" -ll && echo xa | ./a3
$LEX -q one.l 2>&1; echo "status $?"
$LEX nosuch.l 2>&1; echo "status $?"
# diagnostics name the file and line within it
printf '%%%%\na\tECHO;\n(b\tECHO;\n' > bad2.l
$LEX defs.l bad2.l 2>&1; echo "status $?"
