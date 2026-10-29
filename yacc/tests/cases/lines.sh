# #line directives refer to the grammar; -l removes them
cat > g.y <<'G'
%{
int x;
%}
%%
s : 'a' { x = 1; }
  ;
%%
int yylex(void) { return 0; }
void yyerror(const char *s) { (void)s; }
int main(void) { return yyparse(); }
G
$YACC g.y && grep '#line' y.tab.c | sed 's/^#line [0-9]* "y.tab.c"/#line N "y.tab.c"/' | sort | uniq -c
$CC $CWARN -c y.tab.c || exit 1
$YACC -l g.y && grep -c '#line' y.tab.c
$CC $CWARN -c y.tab.c && echo compiled
# the line numbers of the code file itself are right
$YACC g.y && awk '/^#line [0-9]+ "y.tab.c"/ { if ($2 != NR + 1) print "wrong", NR, $2 }' y.tab.c
