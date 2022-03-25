%option reentrant bison-bridge bison-locations
%option noyywrap
%option never-interactive
%option stack
%option nodefault
%option nounput noyy_top_state


%s DEFAULT
%x STRING
%x IND_STRING
%x INPATH
%x INPATH_SLASH
%x PATH_START


%{
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunneeded-internal-declaration"
#endif

#include <boost/lexical_cast.hpp>

#include "nixexpr.hh"
#include "parser-tab.hh"

using namespace nix;

namespace nix {

static inline Pos makeCurPos(const YYLTYPE & loc, ParseData * data)
{
    return Pos(data->origin, data->file, loc.first_line, loc.first_column);
}

#define CUR_POS makeCurPos(*yylloc, data)

// backup to recover from yyless(0)
YYLTYPE prev_yylloc;

static void initLoc(YYLTYPE * loc)
{
    loc->first_line = loc->last_line = 1;
    loc->first_column = loc->last_column = 1;
}

static void adjustLoc(YYLTYPE * loc, const char * s, size_t len)
{
    prev_yylloc = *loc;

    loc->first_line = loc->last_line;
    loc->first_column = loc->last_column;

    for (size_t i = 0; i < len; i++) {
       switch (*s++) {
       case '\r':
           if (*s == '\n') { /* cr/lf */
               i++;
               s++;
           }
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


// we make use of the fact that the parser receives a private copy of the input
// string and can munge around in it.
static StringToken unescapeStr(SymbolTable & symbols, char * s, size_t length)
{
    char * result = s;
    char * t = s;
    char c;
    // the input string is terminated with *two* NULs, so we can safely take
    // *one* character after the one being checked against.
    while ((c = *s++)) {
        if (c == '\\') {
            c = *s++;
            if (c == 'n') *t = '\n';
            else if (c == 'r') *t = '\r';
            else if (c == 't') *t = '\t';
            else *t = c;
        }
        else if (c == '\r') {
            /* Normalise CR and CR/LF into LF. */
            *t = '\n';
            if (*s == '\n') s++; /* cr/lf */
        }
        else *t = c;
        t++;
    }
    return {result, size_t(t - result)};
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
PATH_CHAR   [a-zA-Z0-9\.\_\-\+]
PATH        {PATH_CHAR}*(\/{PATH_CHAR}+)+\/?
PATH_SEG    {PATH_CHAR}*\/
HPATH       \~(\/{PATH_CHAR}+)+\/?
HPATH_START \~\/
SPATH       \<{PATH_CHAR}+(\/{PATH_CHAR}+)*\>
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

{ID}        { yylval->id = {yytext, (size_t) yyleng}; return ID; }
{INT}       { errno = 0;
              try {
                  yylval->n = boost::lexical_cast<int64_t>(yytext);
              } catch (const boost::bad_lexical_cast &) {
                  throw ParseError({
                      .msg = hintfmt("invalid integer '%1%'", yytext),
                      .errPos = CUR_POS,
                  });
              }
              return INT;
            }
{FLOAT}     { errno = 0;
              yylval->nf = strtod(yytext, 0);
              if (errno != 0)
                  throw ParseError({
                      .msg = hintfmt("invalid float '%1%'", yytext),
                      .errPos = CUR_POS,
                  });
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
                yylval->str = unescapeStr(data->symbols, yytext, yyleng);
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
                   yylval->str = {yytext, (size_t) yyleng, true};
                   return IND_STR;
                 }
<IND_STRING>\'\'\$ |
<IND_STRING>\$   {
                   yylval->str = {"$", 1};
                   return IND_STR;
                 }
<IND_STRING>\'\'\' {
                   yylval->str = {"''", 2};
                   return IND_STR;
                 }
<IND_STRING>\'\'\\{ANY} {
                   yylval->str = unescapeStr(data->symbols, yytext + 2, yyleng - 2);
                   return IND_STR;
                 }
<IND_STRING>\$\{ { PUSH_STATE(DEFAULT); return DOLLAR_CURLY; }
<IND_STRING>\'\' { POP_STATE(); return IND_STRING_CLOSE; }
<IND_STRING>\'   {
                   yylval->str = {"'", 1};
                   return IND_STR;
                 }

{PATH_SEG}\$\{ |
{HPATH_START}\$\{ {
  PUSH_STATE(PATH_START);
  yyless(0);
  *yylloc = prev_yylloc;
}

<PATH_START>{PATH_SEG} {
  POP_STATE();
  PUSH_STATE(INPATH_SLASH);
  yylval->path = {yytext, (size_t) yyleng};
  return PATH;
}

<PATH_START>{HPATH_START} {
  POP_STATE();
  PUSH_STATE(INPATH_SLASH);
  yylval->path = {yytext, (size_t) yyleng};
  return HPATH;
}

{PATH} {
  if (yytext[yyleng-1] == '/')
    PUSH_STATE(INPATH_SLASH);
  else
    PUSH_STATE(INPATH);
  yylval->path = {yytext, (size_t) yyleng};
  return PATH;
}
{HPATH} {
  if (yytext[yyleng-1] == '/')
    PUSH_STATE(INPATH_SLASH);
  else
    PUSH_STATE(INPATH);
  yylval->path = {yytext, (size_t) yyleng};
  return HPATH;
}

<INPATH,INPATH_SLASH>\$\{ {
  POP_STATE();
  PUSH_STATE(INPATH);
  PUSH_STATE(DEFAULT);
  return DOLLAR_CURLY;
}
<INPATH,INPATH_SLASH>{PATH}|{PATH_SEG}|{PATH_CHAR}+ {
  POP_STATE();
  if (yytext[yyleng-1] == '/')
      PUSH_STATE(INPATH_SLASH);
  else
      PUSH_STATE(INPATH);
  yylval->str = {yytext, (size_t) yyleng};
  return STR;
}
<INPATH>{ANY} |
<INPATH><<EOF>> {
  /* if we encounter a non-path character we inform the parser that the path has
     ended with a PATH_END token and re-parse this character in the default
     context (it may be ')', ';', or something of that sort) */
  POP_STATE();
  yyless(0);
  *yylloc = prev_yylloc;
  return PATH_END;
}

<INPATH_SLASH>{ANY} |
<INPATH_SLASH><<EOF>> {
  throw ParseError({
      .msg = hintfmt("path has a trailing slash"),
      .errPos = CUR_POS,
  });
}

{SPATH}     { yylval->path = {yytext, (size_t) yyleng}; return SPATH; }
{URI}       { yylval->uri = {yytext, (size_t) yyleng}; return URI; }

[ \t\r\n]+    /* eat up whitespace */
\#[^\r\n]*    /* single-line comments */
\/\*([^*]|\*+[^*/])*\*+\/  /* long comments */

{ANY}       {
              /* Don't return a negative number, as this will cause
                 Bison to stop parsing without an error. */
              return (unsigned char) yytext[0];
            }

%%
