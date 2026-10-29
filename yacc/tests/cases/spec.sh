# the grammars in POSIX.1-2024 (awk, sh, bc, at, yacc, localedef, regular
# expressions): conflicts and the generated parsers compile
for g in "$GRAMMARS"/*.y; do
    b=$(basename "$g" .y)
    cp "$g" .
    echo "== $b"
    $YACC -dv -b "$b" "$b.y" 2>&1 | sed 's/^/  /'
    grep -E '^[0-9]+ (terminals|grammar rules)' "$b.output"
    printf 'int yylex(void) { return 0; }\nvoid yyerror(const char *s) { (void)s; }\nint main(void) { return yyparse(); }\n' >> "$b.tab.c"
    $CC $CQUIET -o "$b" "$b.tab.c" || echo "  does not compile"
done
