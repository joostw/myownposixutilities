D			[0-9]
L			[a-zA-Z_]
H			[a-fA-F0-9]
E			[Ee][+-]?{D}+
FS			(f|F|l|L)
IS			(u|U|l|L)*

%{
#include <stdio.h>
static int col;
static void count(void);
%}

%x COMMENT
%%
"/*"			{ count(); BEGIN COMMENT; }
<COMMENT>"*/"		{ count(); BEGIN INITIAL; printf("COMMENT\n"); }
<COMMENT>.|\n		{ count(); }
"//".*			{ count(); printf("LINECOMMENT\n"); }
^[ \t]*#.*		{ count(); printf("PP %s\n", yytext); }

"auto"			{ count(); printf("AUTO\n"); }
"break"			{ count(); printf("BREAK\n"); }
"case"			{ count(); printf("CASE\n"); }
"char"			{ count(); printf("CHAR\n"); }
"const"			{ count(); printf("CONST\n"); }
"continue"		{ count(); printf("CONTINUE\n"); }
"default"		{ count(); printf("DEFAULT\n"); }
"do"			{ count(); printf("DO\n"); }
"double"		{ count(); printf("DOUBLE\n"); }
"else"			{ count(); printf("ELSE\n"); }
"enum"			{ count(); printf("ENUM\n"); }
"extern"		{ count(); printf("EXTERN\n"); }
"float"			{ count(); printf("FLOAT\n"); }
"for"			{ count(); printf("FOR\n"); }
"goto"			{ count(); printf("GOTO\n"); }
"if"			{ count(); printf("IF\n"); }
"int"			{ count(); printf("INT\n"); }
"long"			{ count(); printf("LONG\n"); }
"register"		{ count(); printf("REGISTER\n"); }
"return"		{ count(); printf("RETURN\n"); }
"short"			{ count(); printf("SHORT\n"); }
"signed"		{ count(); printf("SIGNED\n"); }
"sizeof"		{ count(); printf("SIZEOF\n"); }
"static"		{ count(); printf("STATIC\n"); }
"struct"		{ count(); printf("STRUCT\n"); }
"switch"		{ count(); printf("SWITCH\n"); }
"typedef"		{ count(); printf("TYPEDEF\n"); }
"union"			{ count(); printf("UNION\n"); }
"unsigned"		{ count(); printf("UNSIGNED\n"); }
"void"			{ count(); printf("VOID\n"); }
"volatile"		{ count(); printf("VOLATILE\n"); }
"while"			{ count(); printf("WHILE\n"); }

{L}({L}|{D})*		{ count(); printf("ID %s\n", yytext); }

0[xX]{H}+{IS}?		{ count(); printf("HEX %s\n", yytext); }
0{D}+{IS}?		{ count(); printf("OCT %s\n", yytext); }
{D}+{IS}?		{ count(); printf("INT %s\n", yytext); }
L?'(\\.|[^\\'\n])+'	{ count(); printf("CHARLIT %s\n", yytext); }

{D}+{E}{FS}?		{ count(); printf("FLT %s\n", yytext); }
{D}*"."{D}+({E})?{FS}?	{ count(); printf("FLT %s\n", yytext); }
{D}+"."{D}*({E})?{FS}?	{ count(); printf("FLT %s\n", yytext); }

L?\"(\\.|[^\\"\n])*\"	{ count(); printf("STR %s\n", yytext); }

"..."			{ count(); printf("ELLIPSIS\n"); }
">>="			|
"<<="			|
"+="			|
"-="			|
"*="			|
"/="			|
"%="			|
"&="			|
"^="			|
"|="			{ count(); printf("ASSIGNOP %s\n", yytext); }
">>"			|
"<<"			|
"++"			|
"--"			|
"->"			|
"&&"			|
"||"			|
"<="			|
">="			|
"=="			|
"!="			{ count(); printf("OP2 %s\n", yytext); }
";"|"{"|"<%"|"}"|"%>"|","|":"|"="|"("|")"|"["|"<:"|"]"|":>"|"."|"&"|"!"|"~"|"-"|"+"|"*"|"/"|"%"|"<"|">"|"^"|"|"|"?"	{ count(); printf("OP %s\n", yytext); }

[ \t\v\n\f]		{ count(); }
.			{ count(); printf("BAD %d\n", yytext[0]); }

%%
static void count(void)
{
	int i;
	for (i = 0; yytext[i] != '\0'; i++)
		if (yytext[i] == '\n')
			col = 0;
		else if (yytext[i] == '\t')
			col += 8 - (col % 8);
		else
			col++;
}
int yywrap(void) { printf("col %d\n", col); return 1; }
