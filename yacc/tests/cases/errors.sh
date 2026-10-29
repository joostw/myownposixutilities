# diagnostics for grammars in error; the exit status is non-zero and no code file is written
t() {
    printf '%b' "$1" > bad.y
    rm -f y.tab.c
    $YACC bad.y 2>&1; s=$?
    [ -f y.tab.c ] && echo "  (code file written)"
    echo "  status $s"
}
t '%token A\n%%\n'
t '%token A\ns : A ;\n'
t '%%\ns : t ;\n'
t '%token A\n%%\nA : s ;\ns : ;\n'
t '%%\ns : x\n'
t '%union { int i; char *p; }\n%type <i> s\n%token <p> A\n%%\ns : A ;\n'
t '%union { int i; }\n%type <i> s\n%%\ns : s '"'"'a'"'"' { $$ = $3; } | ;\n'
t '%union { int i; }\n%type <i> s\n%%\ns : { $$ = $0; } ;\n'
t '%union { int i; }\n%type <i> s\n%%\ns : '"'"'a'"'"' { $<i>$ = 1; } { $$ = $2; } | '"'"'b'"'"' { $$ = 0; } ;\n'
t '%union { int i; }\n%type <i> s\n%%\ns : '"'"'a'"'"' { $$ = 1; } { $$ = $2; } ;\n'
t '%token A 300 B 300\n%%\ns : A B ;\n'
t '%token A\n%token A 300\n%%\ns : A ;\n'
t '%left A\n%right A\n%%\ns : A ;\n'
t '%type <x> s\n%type <y> s\n%%\ns : ;\n'
t '%start A\n%token A\n%%\ns : A ;\n'
t '%%\ns : { x ;\n'
t '%{ int x;\n'
t '%%\ns : '"'"'ab\n'
t '%%\ns : '"'"''"'"' ;\n'
t '%%\ns : '"'"'\\q'"'"' ;\n'
t '%%\ns : '"'"'\\x'"'"' ;\n'
t '%%\ns : '"'"'\\777'"'"' ;\n'
t '%%\ns : a %prec b ;\na : ;\n'
t '%token X\n%%\ns : %prec X %prec X ;\n'
t '%foo\n%%\ns : ;\n'
t '%%\n'
t '%%\n| s ;\n'
t '%token <> A\n%%\ns : A ;\n'
t '%%\ns : /* unterminated\n'
t '%%\ns : '"'"'a'"'"' : ;\n'
t '%%\ns : "" ;\n'
t '%union { int i; }\n%union { int j; }\n%%\ns : ;\n'
t '%token A 0\n%token B 0\n%%\ns : A ;\n'
# usage errors
$YACC 2>&1; echo "  status $?"
$YACC -x g.y 2>&1; echo "  status $?"
$YACC a.y b.y 2>&1; echo "  status $?"
$YACC -b 2>&1; echo "  status $?"
$YACC nonexistent.y 2>&1; echo "  status $?"
