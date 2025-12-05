%option 8bit
%option reentrant bison-bridge bison-locations
%option align
%option noyywrap
%option never-interactive
%option stack
%option nodefault
%option nounput noyy_top_state
%option extra-type="::nix::LexerState *"

%s DEFAULT
%x STRING
%x IND_STRING
%x INPATH
%x INPATH_SLASH
%x PATH_START

%top {
#include "parser-tab.hh" // YYSTYPE
#include "nix/expr/parser-state.hh"
}

%{
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunneeded-internal-declaration"
#endif

#include "nix/expr/nixexpr.hh"
#include "lexer-helpers.hh"

namespace nix {
  struct LexerState;
}

using namespace nix;
using namespace nix::lexer::internal;

namespace nix {

#define CUR_POS state->at(*yylloc)

// we make use of the fact that the parser receives a private copy of the input
// string and can munge around in it.
// getting the position is expensive and thus it is implemented lazily.
static StringToken unescapeStr(char * const s, size_t length, std::function<Pos()> && pos)
{
    bool noNullByte = true;
    char * t = s;
    // the input string is terminated with *two* NULs, so we can safely take
    // *one* character after the one being checked against.
    for (size_t i = 0; i < length; t++) {
        char c = s[i++];
        noNullByte &= c != '\0';
        if (c == '\\') {
            c = s[i++];
            if (c == 'n') *t = '\n';
            else if (c == 'r') *t = '\r';
            else if (c == 't') *t = '\t';
            else *t = c;
        }
        else if (c == '\r') {
            /* Normalise CR and CR/LF into LF. */
            *t = '\n';
            if (s[i] == '\n') i++; /* cr/lf */
        }
        else *t = c;
    }
    if (!noNullByte) {
        forceNoNullByte({s, size_t(t - s)}, std::move(pos));
    }
    return {s, size_t(t - s)};
}

static void requireExperimentalFeature(const ExperimentalFeature & feature, const Pos & pos)
{
    if (!experimentalFeatureSettings.isEnabled(feature))
        throw ParseError(ErrorInfo{
            .msg = HintFmt("experimental Nix feature '%1%' is disabled; add '--extra-experimental-features %1%' to enable it", showExperimentalFeature(feature)),
            .pos = pos,
        });
}

}

using enum nix::Parser::token::token_kind_type;
using YYSTYPE = nix::Parser::value_type;
using YYLTYPE = nix::Parser::location_type;

// yacc generates code that uses unannotated fallthrough.
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"

#define YY_USER_INIT initLoc(yylloc)
#define YY_USER_ACTION adjustLoc(yyscanner, yylloc, yytext, yyleng);

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
in          { return IN_KW; }
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
\<\|        { requireExperimentalFeature(Xp::PipeOperators, state->positions[CUR_POS]);
              return PIPE_FROM;
            }
\|\>        { requireExperimentalFeature(Xp::PipeOperators, state->positions[CUR_POS]);
              return PIPE_INTO;
            }

{ID}        { yylval->emplace<StringToken>(yytext, (size_t) yyleng); return ID; }
{INT}       { errno = 0;
              std::optional<int64_t> numMay = string2Int<int64_t>(yytext);
              if (numMay.has_value()) {
                  yylval->emplace<NixInt>(*numMay);
              } else {
                  throw ParseError(ErrorInfo{
                      .msg = HintFmt("invalid integer '%1%'", yytext),
                      .pos = state->positions[CUR_POS],
                  });
              }
              return INT_LIT;
            }
{FLOAT}     { errno = 0;
              yylval->emplace<NixFloat>(strtod(yytext, 0));
              if (errno != 0)
                  throw ParseError(ErrorInfo{
                      .msg = HintFmt("invalid float '%1%'", yytext),
                      .pos = state->positions[CUR_POS],
                  });
              return FLOAT_LIT;
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
                yylval->emplace<StringToken>(unescapeStr(yytext, yyleng, [&]() { return state->positions[CUR_POS]; }));
                return STR;
              }
<STRING>\$\{  { PUSH_STATE(DEFAULT); return DOLLAR_CURLY; }
<STRING>\"    { POP_STATE(); return '"'; }
<STRING>\$|\\|\$\\ {
                /* This can only occur when we reach EOF, otherwise the above
                   (...|\$[^\{\"\\]|\\.|\$\\.)+ would have triggered.
                   This is technically invalid, but we leave the problem to the
                   parser who fails with exact location. */
                return EOF;
              }

\'\'(\ *\n)?     { PUSH_STATE(IND_STRING); return IND_STRING_OPEN; }
<IND_STRING>([^\$\']|\$[^\{\']|\'[^\'\$])+ {
                   yylval->emplace<StringToken>(yytext, (size_t) yyleng, true);
                   forceNoNullByte(yylval->as<StringToken>(), [&]() { return state->positions[CUR_POS]; });
                   return IND_STR;
                 }
<IND_STRING>\'\'\$ |
<IND_STRING>\$   {
                   yylval->emplace<StringToken>("$", 1);
                   return IND_STR;
                 }
<IND_STRING>\'\'\' {
                   yylval->emplace<StringToken>("''", 2);
                   return IND_STR;
                 }
<IND_STRING>\'\'\\{ANY} {
                   yylval->emplace<StringToken>(unescapeStr(yytext + 2, yyleng - 2, [&]() { return state->positions[CUR_POS]; }));
                   return IND_STR;
                 }
<IND_STRING>\$\{ { PUSH_STATE(DEFAULT); return DOLLAR_CURLY; }
<IND_STRING>\'\' { POP_STATE(); return IND_STRING_CLOSE; }
<IND_STRING>\'   {
                   yylval->emplace<StringToken>("'", 1);
                   return IND_STR;
                 }

{PATH_SEG}\$\{ |
{HPATH_START}\$\{ {
  PUSH_STATE(PATH_START);
  yyless(0);
  yylloc->unstash();
}

<PATH_START>{PATH_SEG} {
  POP_STATE();
  PUSH_STATE(INPATH_SLASH);
  yylval->emplace<StringToken>(yytext, (size_t) yyleng);
  return PATH;
}

<PATH_START>{HPATH_START} {
  POP_STATE();
  PUSH_STATE(INPATH_SLASH);
  yylval->emplace<StringToken>(yytext, (size_t) yyleng);
  return HPATH;
}

<PATH_START>{ANY} |
<PATH_START><<EOF>> {
  /* This should be unreachable: PATH_START is only entered after matching
     PATH_SEG or HPATH_START, and we rewind to re-parse those same patterns.
     This rule exists to satisfy flex's %option nodefault requirement. */
  unreachable();
}

{PATH} {
  if (yytext[yyleng-1] == '/')
    PUSH_STATE(INPATH_SLASH);
  else
    PUSH_STATE(INPATH);
  yylval->emplace<StringToken>(yytext, (size_t) yyleng);
  return PATH;
}
{HPATH} {
  if (yytext[yyleng-1] == '/')
    PUSH_STATE(INPATH_SLASH);
  else
    PUSH_STATE(INPATH);
  yylval->emplace<StringToken>(yytext, (size_t) yyleng);
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
  yylval->emplace<StringToken>(yytext, (size_t) yyleng);
  return STR;
}
<INPATH>{ANY} |
<INPATH><<EOF>> {
  /* if we encounter a non-path character we inform the parser that the path has
     ended with a PATH_END token and re-parse this character in the default
     context (it may be ')', ';', or something of that sort) */
  POP_STATE();
  yyless(0);
  yylloc->unstash();
  return PATH_END;
}

<INPATH_SLASH>{ANY} |
<INPATH_SLASH><<EOF>> {
  throw ParseError(ErrorInfo{
      .msg = HintFmt("path has a trailing slash"),
      .pos = state->positions[CUR_POS],
  });
}

{SPATH}     { yylval->emplace<StringToken>(yytext, (size_t) yyleng); return SPATH; }
{URI}       { yylval->emplace<StringToken>(yytext, (size_t) yyleng); return URI; }

%{
// Doc comment rule
//
//   \/\*\*                            /**
//         [^/*]                       reject /**/ (empty comment) and /***
//              ([^*]|\*+[^*/])*\*+\/  same as the long comment rule
//              (             )*       zero or more non-ending sequences
//                              \*     end(1)
//                                 \/  end(2)
%}
\/\*\*[^/*]([^*]|\*+[^*/])*\*+\/  /* doc comments */ {
    LexerState & lexerState = *yyget_extra(yyscanner);
    lexerState.docCommentDistance = 0;
    lexerState.lastDocCommentLoc.beginOffset = yylloc->beginOffset;
    lexerState.lastDocCommentLoc.endOffset = yylloc->endOffset;
}


%{
// The following rules have docCommentDistance--
// This compensates for the docCommentDistance++ which happens by default to
// make all the other rules invalidate the doc comment.
%}
[ \t\r\n]+    /* eat up whitespace */ { yyget_extra(yyscanner)->docCommentDistance--; }
\#[^\r\n]*    /* single-line comments */ { yyget_extra(yyscanner)->docCommentDistance--; }
\/\*([^*]|\*+[^*/])*\*+\/  /* long comments */ { yyget_extra(yyscanner)->docCommentDistance--; }

{ANY}       {
              /* Don't return a negative number, as this will cause
                 Bison to stop parsing without an error. */
              return (unsigned char) yytext[0];
            }

%%
