# the description file: grammar, states, conflicts, resolutions, summary
cat > g.y <<'G'
%token NUM IF ELSE
%nonassoc '<'
%left '+'
%%
s : IF e s | IF e s ELSE s | e ';' ;
e : e '+' e | e '<' e | NUM | NUM '(' ')' ;
G
$YACC -v g.y 2>&1; echo "status $?"
cat y.output
# with an error in the grammar the description file still has a summary
printf '%%%%\ns : t ;\n' > bad.y
rm -f y.output
$YACC -v bad.y 2>&1; echo "status $?"
cat y.output
