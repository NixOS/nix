%option reentrant bison-bridge bison-locations
%option noyywrap
%option never-interactive


%x STRING


%{
#include <string.h>
#include <aterm2.h>
#include "parser-tab.h"

static void initLoc(YYLTYPE * loc)
{
    loc->first_line = 1;
    loc->first_column = 1;
}

static void adjustLoc(YYLTYPE * loc, const char * s, size_t len)
{
    while (len--) {
       switch (*s++) {
       case '\n': 
           ++loc->first_line;
           loc->first_column = 1;
           break;
       default:
           ++loc->first_column;
       }
    }
}

ATerm toATerm(const char * s);
ATerm unescapeStr(const char * s);

#define YY_USER_INIT initLoc(yylloc)
#define YY_USER_ACTION adjustLoc(yylloc, yytext, yyleng);

%}


ID          [a-zA-Z\_][a-zA-Z0-9\_\']*
INT         [0-9]+
PATH        [a-zA-Z0-9\.\_\-\+]*(\/[a-zA-Z0-9\.\_\-\+]+)+
URI         [a-zA-Z][a-zA-Z0-9\+\-\.]*\:[a-zA-Z0-9\%\/\?\:\@\&\=\+\$\,\-\_\.\!\~\*\']+


%%


if          { return IF; }
then        { return THEN; }
else        { return ELSE; }
assert      { return ASSERT; }
with        { return WITH; }
let         { return LET; }
rec         { return REC; }
inherit     { return INHERIT; }

\=\=        { return EQ; }
\!\=        { return NEQ; }
\&\&        { return AND; }
\|\|        { return OR; }
\-\>        { return IMPL; }
\/\/        { return UPDATE; }
\+\+        { return CONCAT; }

{ID}        { yylval->t = toATerm(yytext); return ID; /* !!! alloc */ }
{INT}       { int n = atoi(yytext); /* !!! overflow */
              yylval->t = ATmake("<int>", n);
              return INT;
            }

\"          { BEGIN(STRING); return '"'; }
<STRING>([^\$\"\\]|\\.|\$[^\{\$])+ {
/* Note: a dollar *is* allowed as-is in a string, as long as it's
   not followed by a open brace.  This should probably be disallowed
   eventually. */
              yylval->t = unescapeStr(yytext); /* !!! alloc */ 
              return STR;
            }
<STRING>\$\{  { BEGIN(INITIAL); return DOLLAR_CURLY; }
<STRING>\"  { BEGIN(INITIAL); return '"'; }
<STRING>.   return yytext[0]; /* just in case: shouldn't be reached */


{PATH}      { yylval->t = toATerm(yytext); return PATH; /* !!! alloc */ }
{URI}       { yylval->t = toATerm(yytext); return URI; /* !!! alloc */ }

[ \t\n]+    /* eat up whitespace */
\#[^\n]*    /* single-line comments */
\/\*([^*]|\*[^\/])*\*\/  /* long comments */

.           return yytext[0];


%%

/* Horrible, disgusting hack: allow the parser to set the scanner
   start condition back to STRING.  Necessary in interpolations like
   "foo${expr}bar"; after the close brace we have to go back to the
   STRING state. */
void backToString(yyscan_t scanner)
{
    struct yyguts_t * yyg = (struct yyguts_t*) scanner;
    BEGIN(STRING);
}
