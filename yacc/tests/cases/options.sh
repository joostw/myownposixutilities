# option syntax: grouped flags, attached and separate arguments, --
printf '%%%%\ns : ;\n' > g.y
$YACC -dvt -bfoo g.y && ls; rm -f foo.*
$YACC -d -v -b bar -- g.y && ls; rm -f bar.*
$YACC -vpzz g.y && ls && grep -c 'define yyparse zzparse' y.tab.c; rm -f y.*
mkdir sub && $YACC -b sub/x g.y && ls sub
cp g.y ./-g.y && $YACC -- -g.y && ls y.tab.c
