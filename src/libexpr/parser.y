%glr-parser
%define api.pure
%locations
%define parse.error verbose
%defines
/* %no-lines */
%parse-param { void * scanner }
%parse-param { nix::ParserState * state }
%lex-param { void * scanner }
%lex-param { nix::ParserState * state }
%expect 1
%expect-rr 1

%code requires {

#ifndef BISON_HEADER
#define BISON_HEADER

#include <variant>

#include "finally.hh"
#include "util.hh"
#include "users.hh"

#include "nixexpr.hh"
#include "eval.hh"
#include "eval-settings.hh"
#include "globals.hh"
#include "parser-state.hh"

#define YYLTYPE ::nix::ParserLocation
#define YY_DECL int yylex \
    (YYSTYPE * yylval_param, YYLTYPE * yylloc_param, yyscan_t yyscanner, nix::ParserState * state)

namespace nix {

Expr * parseExprFromBuf(
    char * text,
    size_t length,
    Pos::Origin origin,
    const SourcePath & basePath,
    SymbolTable & symbols,
    PosTable & positions,
    const ref<InputAccessor> rootFS,
    const Expr::AstSymbols & astSymbols);

}

#endif

}

%{

#include "parser-tab.hh"
#include "lexer-tab.hh"

YY_DECL;

using namespace nix;

#define CUR_POS state->at(*yylocp)

// otherwise destructors cause compiler errors
#pragma GCC diagnostic ignored "-Wswitch-enum"

#define THROW(err, ...)                              \
  do {                                               \
    state->error.reset(new auto(err));               \
    [](auto... d) { (delete d, ...); }(__VA_ARGS__); \
    YYABORT;                                         \
  } while (0)

void yyerror(YYLTYPE * loc, yyscan_t scanner, ParserState * state, const char * error)
{
    if (std::string_view(error).starts_with("syntax error, unexpected end of file")) {
        loc->first_column = loc->last_column;
        loc->first_line = loc->last_line;
    }
    throw ParseError({
        .msg = HintFmt(error),
        .pos = state->positions[state->at(*loc)]
    });
}

template<typename T>
static std::unique_ptr<T> unp(T * e)
{
  return std::unique_ptr<T>(e);
}

template<typename T = std::unique_ptr<nix::Expr>, typename... Args>
static std::vector<T> vec(Args && ... args)
{
  std::vector<T> result;
  result.reserve(sizeof...(Args));
  (result.emplace_back(std::forward<Args>(args)), ...);
  return result;
}


%}

%union {
  // !!! We're probably leaking stuff here.
  nix::Expr * e;
  nix::ExprList * list;
  nix::ExprAttrs * attrs;
  nix::Formals * formals;
  nix::Formal * formal;
  nix::NixInt n;
  nix::NixFloat nf;
  nix::StringToken id; // !!! -> Symbol
  nix::StringToken path;
  nix::StringToken uri;
  nix::StringToken str;
  std::vector<nix::AttrName> * attrNames;
  std::vector<std::pair<nix::AttrName, nix::PosIdx>> * inheritAttrs;
  std::vector<std::pair<nix::PosIdx, std::unique_ptr<nix::Expr>>> * string_parts;
  std::vector<std::pair<nix::PosIdx, std::variant<std::unique_ptr<nix::Expr>, nix::StringToken>>> * ind_string_parts;
}

%destructor { delete $$; } <e>
%destructor { delete $$; } <list>
%destructor { delete $$; } <attrs>
%destructor { delete $$; } <formals>
%destructor { delete $$; } <formal>
%destructor { delete $$; } <attrNames>
%destructor { delete $$; } <inheritAttrs>
%destructor { delete $$; } <string_parts>
%destructor { delete $$; } <ind_string_parts>

%type <e> start
%type <e> expr expr_function expr_if expr_op
%type <e> expr_select expr_simple expr_app
%type <list> expr_list
%type <attrs> binds
%type <formals> formals
%type <formal> formal
%type <attrNames> attrpath
%type <inheritAttrs> attrs
%type <string_parts> string_parts_interpolated
%type <ind_string_parts> ind_string_parts
%type <e> path_start string_parts string_attr
%type <id> attr
%token <id> ID
%token <str> STR IND_STR
%token <n> INT_LIT
%token <nf> FLOAT_LIT
%token <path> PATH HPATH SPATH PATH_END
%token <uri> URI
%token IF THEN ELSE ASSERT WITH LET IN_KW REC INHERIT EQ NEQ AND OR IMPL OR_KW
%token DOLLAR_CURLY /* == ${ */
%token IND_STRING_OPEN IND_STRING_CLOSE
%token ELLIPSIS

%right IMPL
%left OR
%left AND
%nonassoc EQ NEQ
%nonassoc '<' '>' LEQ GEQ
%right UPDATE
%left NOT
%left '+' '-'
%left '*' '/'
%right CONCAT
%nonassoc '?'
%nonassoc NEGATE

%%

start: expr { state->result = $1; $$ = 0; };

expr: expr_function;

expr_function
  : ID ':' expr_function
    { $$ = new ExprLambda(CUR_POS, state->symbols.create($1), nullptr, unp($3)); }
  | '{' formals '}' ':' expr_function
    { if (auto e = state->validateFormals($2)) THROW(*e);
      $$ = new ExprLambda(CUR_POS, unp($2), unp($5));
    }
  | '{' formals '}' '@' ID ':' expr_function
    {
      auto arg = state->symbols.create($5);
      if (auto e = state->validateFormals($2, CUR_POS, arg)) THROW(*e, $2, $7);
      $$ = new ExprLambda(CUR_POS, arg, unp($2), unp($7));
    }
  | ID '@' '{' formals '}' ':' expr_function
    {
      auto arg = state->symbols.create($1);
      if (auto e = state->validateFormals($4, CUR_POS, arg)) THROW(*e, $4, $7);
      $$ = new ExprLambda(CUR_POS, arg, unp($4), unp($7));
    }
  | ASSERT expr ';' expr_function
    { $$ = new ExprAssert(CUR_POS, unp($2), unp($4)); }
  | WITH expr ';' expr_function
    { $$ = new ExprWith(CUR_POS, unp($2), unp($4)); }
  | LET binds IN_KW expr_function
    { if (!$2->dynamicAttrs.empty())
        THROW(ParseError({
            .msg = HintFmt("dynamic attributes not allowed in let"),
            .pos = state->positions[CUR_POS]
        }), $2, $4);
      $$ = new ExprLet(unp($2), unp($4));
    }
  | expr_if
  ;

expr_if
  : IF expr THEN expr ELSE expr { $$ = new ExprIf(CUR_POS, unp($2), unp($4), unp($6)); }
  | expr_op
  ;

expr_op
  : '!' expr_op %prec NOT { $$ = new ExprOpNot(unp($2)); }
  | '-' expr_op %prec NEGATE { $$ = new ExprCall(CUR_POS, std::make_unique<ExprVar>(state->s.sub), vec(std::make_unique<ExprInt>(0), unp($2))); }
  | expr_op EQ expr_op { $$ = new ExprOpEq(unp($1), unp($3)); }
  | expr_op NEQ expr_op { $$ = new ExprOpNEq(unp($1), unp($3)); }
  | expr_op '<' expr_op { $$ = new ExprCall(state->at(@2), std::make_unique<ExprVar>(state->s.lessThan), vec($1, $3)); }
  | expr_op LEQ expr_op { $$ = new ExprOpNot(std::make_unique<ExprCall>(state->at(@2), std::make_unique<ExprVar>(state->s.lessThan), vec($3, $1))); }
  | expr_op '>' expr_op { $$ = new ExprCall(state->at(@2), std::make_unique<ExprVar>(state->s.lessThan), vec($3, $1)); }
  | expr_op GEQ expr_op { $$ = new ExprOpNot(std::make_unique<ExprCall>(state->at(@2), std::make_unique<ExprVar>(state->s.lessThan), vec($1, $3))); }
  | expr_op AND expr_op { $$ = new ExprOpAnd(state->at(@2), unp($1), unp($3)); }
  | expr_op OR expr_op { $$ = new ExprOpOr(state->at(@2), unp($1), unp($3)); }
  | expr_op IMPL expr_op { $$ = new ExprOpImpl(state->at(@2), unp($1), unp($3)); }
  | expr_op UPDATE expr_op { $$ = new ExprOpUpdate(state->at(@2), unp($1), unp($3)); }
  | expr_op '?' attrpath { $$ = new ExprOpHasAttr(unp($1), std::move(*$3)); delete $3; }
  | expr_op '+' expr_op
    { $$ = new ExprConcatStrings(state->at(@2), false, vec<std::pair<PosIdx, std::unique_ptr<Expr>>>(std::pair(state->at(@1), unp($1)), std::pair(state->at(@3), unp($3)))); }
  | expr_op '-' expr_op { $$ = new ExprCall(state->at(@2), std::make_unique<ExprVar>(state->s.sub), vec($1, $3)); }
  | expr_op '*' expr_op { $$ = new ExprCall(state->at(@2), std::make_unique<ExprVar>(state->s.mul), vec($1, $3)); }
  | expr_op '/' expr_op { $$ = new ExprCall(state->at(@2), std::make_unique<ExprVar>(state->s.div), vec($1, $3)); }
  | expr_op CONCAT expr_op { $$ = new ExprOpConcatLists(state->at(@2), unp($1), unp($3)); }
  | expr_app
  ;

expr_app
  : expr_app expr_select {
      if (auto e2 = dynamic_cast<ExprCall *>($1)) {
          e2->args.emplace_back($2);
          $$ = $1;
      } else
          $$ = new ExprCall(CUR_POS, unp($1), vec(unp($2)));
  }
  | expr_select
  ;

expr_select
  : expr_simple '.' attrpath
    { $$ = new ExprSelect(CUR_POS, unp($1), std::move(*$3), nullptr); delete $3; }
  | expr_simple '.' attrpath OR_KW expr_select
    { $$ = new ExprSelect(CUR_POS, unp($1), std::move(*$3), unp($5)); delete $3; }
  | /* Backwards compatibility: because Nixpkgs has a rarely used
       function named ‘or’, allow stuff like ‘map or [...]’. */
    expr_simple OR_KW
    { $$ = new ExprCall(CUR_POS, unp($1), vec(std::make_unique<ExprVar>(CUR_POS, state->s.or_))); }
  | expr_simple
  ;

expr_simple
  : ID {
      std::string_view s = "__curPos";
      if ($1.l == s.size() && strncmp($1.p, s.data(), s.size()) == 0)
          $$ = new ExprPos(CUR_POS);
      else
          $$ = new ExprVar(CUR_POS, state->symbols.create($1));
  }
  | INT_LIT { $$ = new ExprInt($1); }
  | FLOAT_LIT { $$ = new ExprFloat($1); }
  | '"' string_parts '"' { $$ = $2; }
  | IND_STRING_OPEN ind_string_parts IND_STRING_CLOSE {
      $$ = state->stripIndentation(CUR_POS, std::move(*$2)).release();
      delete $2;
  }
  | path_start PATH_END
  | path_start string_parts_interpolated PATH_END {
      $2->emplace($2->begin(), state->at(@1), $1);
      $$ = new ExprConcatStrings(CUR_POS, false, std::move(*$2));
      delete $2;
  }
  | SPATH {
      std::string path($1.p + 1, $1.l - 2);
      $$ = new ExprCall(CUR_POS,
          std::make_unique<ExprVar>(state->s.findFile),
          vec(std::make_unique<ExprVar>(state->s.nixPath),
              std::make_unique<ExprString>(std::move(path))));
  }
  | URI {
      static bool noURLLiterals = experimentalFeatureSettings.isEnabled(Xp::NoUrlLiterals);
      if (noURLLiterals)
          THROW(ParseError({
              .msg = HintFmt("URL literals are disabled"),
              .pos = state->positions[CUR_POS]
          }));
      $$ = new ExprString(std::string($1));
  }
  | '(' expr ')' { $$ = $2; }
  /* Let expressions `let {..., body = ...}' are just desugared
     into `(rec {..., body = ...}).body'. */
  | LET '{' binds '}'
    { $3->recursive = true; $$ = new ExprSelect(noPos, unp($3), state->s.body); }
  | REC '{' binds '}'
    { $3->recursive = true; $$ = $3; }
  | '{' binds '}'
    { $$ = $2; }
  | '[' expr_list ']' { $$ = $2; }
  ;

string_parts
  : STR { $$ = new ExprString(std::string($1)); }
  | string_parts_interpolated
    { $$ = new ExprConcatStrings(CUR_POS, true, std::move(*$1));
      delete $1;
    }
  | { $$ = new ExprString(""); }
  ;

string_parts_interpolated
  : string_parts_interpolated STR
  { $$ = $1; $1->emplace_back(state->at(@2), new ExprString(std::string($2))); }
  | string_parts_interpolated DOLLAR_CURLY expr '}' { $$ = $1; $1->emplace_back(state->at(@2), $3); }
  | DOLLAR_CURLY expr '}' { $$ = new std::vector<std::pair<PosIdx, std::unique_ptr<Expr>>>; $$->emplace_back(state->at(@1), $2); }
  | STR DOLLAR_CURLY expr '}' {
      $$ = new std::vector<std::pair<PosIdx, std::unique_ptr<Expr>>>;
      $$->emplace_back(state->at(@1), new ExprString(std::string($1)));
      $$->emplace_back(state->at(@2), $3);
    }
  ;

path_start
  : PATH {
    Path path(absPath({$1.p, $1.l}, state->basePath.path.abs()));
    /* add back in the trailing '/' to the first segment */
    if ($1.p[$1.l-1] == '/' && $1.l > 1)
      path += "/";
    $$ = new ExprPath(ref<InputAccessor>(state->rootFS), std::move(path));
  }
  | HPATH {
    if (evalSettings.pureEval) {
        THROW(Error(
            "the path '%s' can not be resolved in pure mode",
            std::string_view($1.p, $1.l)
        ));
    }
    Path path(getHome() + std::string($1.p + 1, $1.l - 1));
    $$ = new ExprPath(ref<InputAccessor>(state->rootFS), std::move(path));
  }
  ;

ind_string_parts
  : ind_string_parts IND_STR { $$ = $1; $1->emplace_back(state->at(@2), $2); }
  | ind_string_parts DOLLAR_CURLY expr '}' { $$ = $1; $1->emplace_back(state->at(@2), unp($3)); }
  | { $$ = new std::vector<std::pair<PosIdx, std::variant<std::unique_ptr<Expr>, StringToken>>>; }
  ;

binds
  : binds attrpath '=' expr ';'
    { $$ = $1;
      if (auto e = state->addAttr($$, std::move(*$2), unp($4), state->at(@2))) THROW(*e, $1, $2);
      delete $2;
    }
  | binds INHERIT attrs ';'
    { $$ = $1;
      for (auto & [i, iPos] : *$3) {
          if ($$->attrs.find(i.symbol) != $$->attrs.end())
              THROW(state->dupAttr(i.symbol, iPos, $$->attrs[i.symbol].pos), $1);
          $$->attrs.emplace(
              i.symbol,
              ExprAttrs::AttrDef(std::make_unique<ExprVar>(iPos, i.symbol), iPos, ExprAttrs::AttrDef::Kind::Inherited));
      }
      delete $3;
    }
  | binds INHERIT '(' expr ')' attrs ';'
    { $$ = $1;
      if (!$$->inheritFromExprs)
          $$->inheritFromExprs = std::make_unique<std::vector<std::unique_ptr<Expr>>>();
      $$->inheritFromExprs->push_back(unp($4));
      for (auto & [i, iPos] : *$6) {
          if ($$->attrs.find(i.symbol) != $$->attrs.end())
              THROW(state->dupAttr(i.symbol, iPos, $$->attrs[i.symbol].pos), $1);
          auto from = std::make_unique<nix::ExprInheritFrom>(state->at(@4), $$->inheritFromExprs->size() - 1);
          $$->attrs.emplace(
              i.symbol,
              ExprAttrs::AttrDef(
                  std::make_unique<ExprSelect>(iPos, std::move(from), i.symbol),
                  iPos,
                  ExprAttrs::AttrDef::Kind::InheritedFrom));
      }
      delete $6;
    }
  | { $$ = new ExprAttrs(state->at(@0)); }
  ;

attrs
  : attrs attr { $$ = $1; $1->emplace_back(AttrName(state->symbols.create($2)), state->at(@2)); }
  | attrs string_attr
    { $$ = $1;
      ExprString * str = dynamic_cast<ExprString *>($2);
      if (str) {
          $$->emplace_back(AttrName(state->symbols.create(str->s)), state->at(@2));
          delete str;
      } else
          THROW(ParseError({
              .msg = HintFmt("dynamic attributes not allowed in inherit"),
              .pos = state->positions[state->at(@2)]
          }), $1, $2);
    }
  | { $$ = new std::vector<std::pair<AttrName, PosIdx>>; }
  ;

attrpath
  : attrpath '.' attr { $$ = $1; $1->push_back(AttrName(state->symbols.create($3))); }
  | attrpath '.' string_attr
    { $$ = $1;
      ExprString * str = dynamic_cast<ExprString *>($3);
      if (str) {
          $$->push_back(AttrName(state->symbols.create(str->s)));
          delete str;
      } else
          $$->emplace_back(unp($3));
    }
  | attr { $$ = new std::vector<AttrName>; $$->push_back(AttrName(state->symbols.create($1))); }
  | string_attr
    { $$ = new std::vector<AttrName>;
      ExprString *str = dynamic_cast<ExprString *>($1);
      if (str) {
          $$->push_back(AttrName(state->symbols.create(str->s)));
          delete str;
      } else
          $$->emplace_back(unp($1));
    }
  ;

attr
  : ID
  | OR_KW { $$ = {"or", 2}; }
  ;

string_attr
  : '"' string_parts '"' { $$ = $2; }
  | DOLLAR_CURLY expr '}' { $$ = $2; }
  ;

expr_list
  : expr_list expr_select { $$ = $1; $1->elems.emplace_back($2); /* !!! dangerous */ }
  | { $$ = new ExprList; }
  ;

formals
  : formal ',' formals
    { $$ = $3; $$->formals.emplace_back(std::move(*$1)); delete $1; }
  | formal
    { $$ = new Formals; $$->formals.emplace_back(std::move(*$1)); $$->ellipsis = false; delete $1; }
  |
    { $$ = new Formals; $$->ellipsis = false; }
  | ELLIPSIS
    { $$ = new Formals; $$->ellipsis = true; }
  ;

formal
  : ID { $$ = new Formal{CUR_POS, state->symbols.create($1), nullptr}; }
  | ID '?' expr { $$ = new Formal{CUR_POS, state->symbols.create($1), unp($3)}; }
  ;

%%

#include "eval.hh"


namespace nix {

Expr * parseExprFromBuf(
    char * text,
    size_t length,
    Pos::Origin origin,
    const SourcePath & basePath,
    SymbolTable & symbols,
    PosTable & positions,
    const ref<InputAccessor> rootFS,
    const Expr::AstSymbols & astSymbols)
{
    yyscan_t scanner;
    ParserState state {
        .symbols = symbols,
        .positions = positions,
        .basePath = basePath,
        .origin = positions.addOrigin(origin, length),
        .rootFS = rootFS,
        .s = astSymbols,
    };

    yylex_init(&scanner);
    Finally _destroy([&] { yylex_destroy(scanner); });

    yy_scan_buffer(text, length, scanner);
    yyparse(scanner, &state);
    if (state.error) {
      delete state.result;
      throw *state.error;
    }

    return state.result;
}


}
