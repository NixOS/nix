%option reentrant bison-bridge bison-locations
%option noyywrap
%option never-interactive


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

#define YY_USER_INIT initLoc(yylloc)
#define YY_USER_ACTION adjustLoc(yylloc, yytext, yyleng);

%}


ID          [a-zA-Z\_][a-zA-Z0-9\_\']*
INT         [0-9]+
STR         \"[^\n\"]*\"
PATH        [a-zA-Z0-9\.\_\-\+]*(\/[a-zA-Z0-9\.\_\-\+]+)+
URI         [a-zA-Z][a-zA-Z0-9\+\-\.]*\:[a-zA-Z0-9\%\/\?\:\@\&\=\+\$\,\-\_\.\!\~\*\']*


%%


if          { return IF; }
then        { return THEN; }
else        { return ELSE; }
assert      { return ASSERT; }
let         { return LET; }
rec         { return REC; }

\=\=        { return EQ; }
\!\=        { return NEQ; }
\&\&        { return AND; }
\|\|        { return OR; }
\-\>        { return IMPL; }

{ID}        { yylval->t = ATmake("<str>", yytext); return ID; /* !!! alloc */ }
{INT}       { int n = atoi(yytext); /* !!! overflow */
              yylval->t = ATmake("<int>", n);
              return INT;
            }
{STR}       { int len = strlen(yytext);
              yytext[len - 1] = 0;
              yylval->t = ATmake("<str>", yytext + 1);
              yytext[len - 1] = '\"';
              return STR; /* !!! alloc */
            }
{PATH}      { yylval->t = ATmake("<str>", yytext); return PATH; /* !!! alloc */ }
{URI}       { yylval->t = ATmake("<str>", yytext); return URI; /* !!! alloc */ }

[ \t\n]+    /* eat up whitespace */
\#[^\n]*    /* single-line comments */
\/\*(.|\n)*\*\/  /* long comments */

.           return yytext[0];


%%
