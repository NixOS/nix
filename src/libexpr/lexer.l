%option reentrant bison-bridge bison-locations
%option noyywrap
%option never-interactive
%option stack
%option nodefault
%option nounput noyy_top_state


%s DEFAULT
%x STRING
%x IND_STRING


%{
#include <boost/lexical_cast.hpp>

#include "nixexpr.hh"
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


static Expr * unescapeStr(SymbolTable & symbols, const char * s, size_t length)
{
    string t;
    t.reserve(length);
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

#define PUSH_STATE(state) yy_push_state(state, yyscanner)
#define POP_STATE() yy_pop_state(yyscanner)

%}


ANY         .|\n
ID          [a-zA-Z\_][a-zA-Z0-9\_\'\-]*
INT         [0-9]+
FLOAT       (([1-9][0-9]*\.[0-9]*)|(0?\.[0-9]+))([Ee][+-]?[0-9]+)?
PATH        [a-zA-Z0-9\.\_\-\+]*(\/[a-zA-Z0-9\.\_\-\+]+)+\/?
HPATH       \~(\/[a-zA-Z0-9\.\_\-\+]+)+\/?
SPATH       \<[a-zA-Z0-9\.\_\-\+]+(\/[a-zA-Z0-9\.\_\-\+]+)*\>
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
or          { return OR_KW; }
\.\.\.      { return ELLIPSIS; }

\=\=        { return EQ; }
\!\=        { return NEQ; }
\<\=        { return LEQ; }
\>\=        { return GEQ; }
\&\&        { return AND; }
\|\|        { return OR; }
\-\>        { return IMPL; }
\/\/        { return UPDATE; }
\+\+        { return CONCAT; }

{ID}        { yylval->id = strdup(yytext); return ID; }
{INT}       { errno = 0;
              try {
                  yylval->n = boost::lexical_cast<int64_t>(yytext);
              } catch (const boost::bad_lexical_cast &) {
                  throw ParseError(format("invalid integer '%1%'") % yytext);
              }
              return INT;
            }
{FLOAT}     { errno = 0;
              yylval->nf = strtod(yytext, 0);
              if (errno != 0)
                  throw ParseError(format("invalid float '%1%'") % yytext);
              return FLOAT;
            }

\$\{        { PUSH_STATE(DEFAULT); return DOLLAR_CURLY; }

\}          { /* State INITIAL only exists at the bottom of the stack and is
                 used as a marker. DEFAULT replaces it everywhere else.
                 Popping when in INITIAL state causes an empty stack exception,
                 so don't */
              if (YYSTATE != INITIAL)
                POP_STATE();
              return '}';
            }
\{          { PUSH_STATE(DEFAULT); return '{'; }

\"          { PUSH_STATE(STRING); return '"'; }
<STRING>([^\$\"\\]|\$[^\{\"\\]|\\{ANY}|\$\\{ANY})*\$/\" |
<STRING>([^\$\"\\]|\$[^\{\"\\]|\\{ANY}|\$\\{ANY})+ {
                /* It is impossible to match strings ending with '$' with one
                   regex because trailing contexts are only valid at the end
                   of a rule. (A sane but undocumented limitation.) */
                yylval->e = unescapeStr(data->symbols, yytext, yyleng);
                return STR;
              }
<STRING>\$\{  { PUSH_STATE(DEFAULT); return DOLLAR_CURLY; }
<STRING>\"    { POP_STATE(); return '"'; }
<STRING>\$|\\|\$\\ {
                /* This can only occur when we reach EOF, otherwise the above
                   (...|\$[^\{\"\\]|\\.|\$\\.)+ would have triggered.
                   This is technically invalid, but we leave the problem to the
                   parser who fails with exact location. */
                return STR;
              }

\'\'(\ *\n)?     { PUSH_STATE(IND_STRING); return IND_STRING_OPEN; }
<IND_STRING>([^\$\']|\$[^\{\']|\'[^\'\$])+ {
                   yylval->e = new ExprIndStr(yytext);
                   return IND_STR;
                 }
<IND_STRING>\'\'\$ |
<IND_STRING>\$   {
                   yylval->e = new ExprIndStr("$");
                   return IND_STR;
                 }
<IND_STRING>\'\'\' {
                   yylval->e = new ExprIndStr("''");
                   return IND_STR;
                 }
<IND_STRING>\'\'\\{ANY} {
                   yylval->e = unescapeStr(data->symbols, yytext + 2, yyleng - 2);
                   return IND_STR;
                 }
<IND_STRING>\$\{ { PUSH_STATE(DEFAULT); return DOLLAR_CURLY; }
<IND_STRING>\'\' { POP_STATE(); return IND_STRING_CLOSE; }
<IND_STRING>\'   {
                   yylval->e = new ExprIndStr("'");
                   return IND_STR;
                 }


{PATH}      { if (yytext[yyleng-1] == '/')
                  throw ParseError("path '%s' has a trailing slash", yytext);
              yylval->path = strdup(yytext);
              return PATH;
            }
{HPATH}     { if (yytext[yyleng-1] == '/')
                  throw ParseError("path '%s' has a trailing slash", yytext);
              yylval->path = strdup(yytext);
              return HPATH;
            }
{SPATH}     { yylval->path = strdup(yytext); return SPATH; }
{URI}       { yylval->uri = strdup(yytext); return URI; }

[ \t\r\n]+    /* eat up whitespace */
\#[^\r\n]*    /* single-line comments */
\/\*([^*]|\*+[^*/])*\*+\/  /* long comments */

{ANY}       {
              /* Don't return a negative number, as this will cause
                 Bison to stop parsing without an error. */
              return (unsigned char) yytext[0];
            }

%%

