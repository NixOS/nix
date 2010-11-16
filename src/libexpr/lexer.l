%option reentrant bison-bridge bison-locations
%option noyywrap
%option never-interactive


%x STRING
%x IND_STRING


%{
#include "nixexpr.hh"
#define BISON_HEADER_HACK
#include "parser-tab.hh"

using namespace nix;

namespace nix {

    
static void initLoc(YYLTYPE * loc)
{
    loc->first_line = loc->last_line = 1;
    loc->first_column = loc->last_column = 1;
}

    
static void adjustLoc(YYLTYPE * loc, const char * s, size_t len)
{
    loc->first_line = loc->last_line;
    loc->first_column = loc->last_column;

    while (len--) {
       switch (*s++) {
       case '\r':
           if (*s == '\n') /* cr/lf */
               s++;
           /* fall through */
       case '\n': 
           ++loc->last_line;
           loc->last_column = 1;
           break;
       default:
           ++loc->last_column;
       }
    }
}


static Expr * unescapeStr(SymbolTable & symbols, const char * s)
{
    string t;
    char c;
    while ((c = *s++)) {
        if (c == '\\') {
            assert(*s);
            c = *s++;
            if (c == 'n') t += '\n';
            else if (c == 'r') t += '\r';
            else if (c == 't') t += '\t';
            else t += c;
        }
        else if (c == '\r') {
            /* Normalise CR and CR/LF into LF. */
            t += '\n';
            if (*s == '\n') s++; /* cr/lf */
        }
        else t += c;
    }
    return new ExprString(symbols.create(t));
}

 
}

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
in          { return IN; }
rec         { return REC; }
inherit     { return INHERIT; }
\.\.\.      { return ELLIPSIS; }

\=\=        { return EQ; }
\!\=        { return NEQ; }
\&\&        { return AND; }
\|\|        { return OR; }
\-\>        { return IMPL; }
\/\/        { return UPDATE; }
\+\+        { return CONCAT; }

{ID}        { yylval->id = strdup(yytext); return ID; }
{INT}       { int n = atoi(yytext); /* !!! overflow */
              yylval->n = n;
              return INT;
            }

\"          { BEGIN(STRING); return '"'; }
<STRING>([^\$\"\\]|\$[^\{\"]|\\.)+ {
              /* !!! Not quite right: we want a follow restriction on
                 "$", it shouldn't be followed by a "{".  Right now
                 "$\"" will be consumed as part of a string, rather
                 than a "$" followed by the string terminator.
                 Disallow "$\"" for now. */
              yylval->e = unescapeStr(data->symbols, yytext);
              return STR;
            }
<STRING>\$\{  { BEGIN(INITIAL); return DOLLAR_CURLY; }
<STRING>\"  { BEGIN(INITIAL); return '"'; }
<STRING>.   return yytext[0]; /* just in case: shouldn't be reached */

\'\'(\ *\n)?     { BEGIN(IND_STRING); return IND_STRING_OPEN; }
<IND_STRING>([^\$\']|\$[^\{\']|\'[^\'\$])+ {
                   yylval->e = new ExprIndStr(yytext);
                   return IND_STR;
                 }
<IND_STRING>\'\'\$ {
                   yylval->e = new ExprIndStr("$");
                   return IND_STR;
                 }
<IND_STRING>\'\'\' {
                   yylval->e = new ExprIndStr("''");
                   return IND_STR;
                 }
<IND_STRING>\'\'\\. {
                   yylval->e = unescapeStr(data->symbols, yytext + 2);
                   return IND_STR;
                 }
<IND_STRING>\$\{ { BEGIN(INITIAL); return DOLLAR_CURLY; }
<IND_STRING>\'\' { BEGIN(INITIAL); return IND_STRING_CLOSE; }
<IND_STRING>\'   {
                   yylval->e = new ExprIndStr("'");
                   return IND_STR;
                 }
<IND_STRING>.    return yytext[0]; /* just in case: shouldn't be reached */

{PATH}      { yylval->path = strdup(yytext); return PATH; }
{URI}       { yylval->uri = strdup(yytext); return URI; }

[ \t\r\n]+    /* eat up whitespace */
\#[^\r\n]*    /* single-line comments */
\/\*([^*]|\*[^\/])*\*\/  /* long comments */

.           return yytext[0];


%%


namespace nix {
    
/* Horrible, disgusting hack: allow the parser to set the scanner
   start condition back to STRING.  Necessary in interpolations like
   "foo${expr}bar"; after the close brace we have to go back to the
   STRING state. */
void backToString(yyscan_t scanner)
{
    struct yyguts_t * yyg = (struct yyguts_t *) scanner;
    BEGIN(STRING);
}

void backToIndString(yyscan_t scanner)
{
    struct yyguts_t * yyg = (struct yyguts_t *) scanner;
    BEGIN(IND_STRING);
}

}
