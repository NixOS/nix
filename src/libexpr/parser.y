%skeleton "lalr1.cc"
%define api.location.type { ::nix::ParserLocation }
%define api.namespace { ::nix::parser }
%define api.parser.class { BisonParser }
%locations
%define parse.error verbose
%defines
/* %no-lines */
%parse-param { void * scanner }
%parse-param { nix::ParserState * state }
%lex-param { void * scanner }
%lex-param { nix::ParserState * state }
%expect 0

%code requires {

// bison adds a bunch of switch statements with default:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

#ifndef BISON_HEADER
#define BISON_HEADER

#include <variant>

#include "nix/util/finally.hh"
#include "nix/util/util.hh"
#include "nix/util/users.hh"

#include "nix/expr/nixexpr.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/expr/parser-state.hh"

#define YY_DECL                                    \
    int yylex(                                     \
        nix::Parser::value_type * yylval_param,    \
        nix::Parser::location_type * yylloc_param, \
        yyscan_t yyscanner,                        \
        nix::ParserState * state)

// For efficiency, we only track offsets; not line,column coordinates
# define YYLLOC_DEFAULT(Current, Rhs, N)                                \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).beginOffset = YYRHSLOC (Rhs, 1).beginOffset;        \
          (Current).endOffset  = YYRHSLOC (Rhs, N).endOffset;           \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).beginOffset = (Current).endOffset =                 \
            YYRHSLOC (Rhs, 0).endOffset;                                \
        }                                                               \
    while (0)

namespace nix {

typedef boost::unordered_flat_map<PosIdx, DocComment, std::hash<PosIdx>> DocCommentMap;

Expr * parseExprFromBuf(
    char * text,
    size_t length,
    Pos::Origin origin,
    const SourcePath & basePath,
    std::pmr::polymorphic_allocator<char> & alloc,
    SymbolTable & symbols,
    const EvalSettings & settings,
    PosTable & positions,
    DocCommentMap & docComments,
    const ref<SourceAccessor> rootFS);

}

#endif

}

%{

/* The parser is very performance sensitive and loses out on a lot
   of performance even with basic stdlib assertions. Since those don't
   affect ABI we can disable those just for this file. */
#if defined(_GLIBCXX_ASSERTIONS) && !defined(_GLIBCXX_DEBUG)
#undef _GLIBCXX_ASSERTIONS
#endif

#include "parser-scanner-decls.hh"

YY_DECL;

using namespace nix;

#define CUR_POS state->at(yylhs.location)

void parser::BisonParser::error(const location_type &loc_, const std::string &error)
{
    auto loc = loc_;
    if (std::string_view(error).starts_with("syntax error, unexpected end of file")) {
        loc.beginOffset = loc.endOffset;
    }
    throw ParseError({
        .msg = HintFmt(error),
        .pos = state->positions[state->at(loc)]
    });
}

#define SET_DOC_POS(lambda, pos) setDocPosition(state->lexerState, lambda, state->at(pos))
static void setDocPosition(const LexerState & lexerState, ExprLambda * lambda, PosIdx start) {
    auto it = lexerState.positionToDocComment.find(start);
    if (it != lexerState.positionToDocComment.end()) {
        lambda->setDocComment(it->second);
    }
}

static Expr * makeCall(PosIdx pos, Expr * fn, Expr * arg) {
    if (auto e2 = dynamic_cast<ExprCall *>(fn)) {
        e2->args.push_back(arg);
        return fn;
    }
    return new ExprCall(pos, fn, {arg});
}


%}

%define api.value.type variant

%type <nix::Expr *> start expr expr_function expr_if expr_op
%type <nix::Expr *> expr_select expr_simple expr_app
%type <nix::Expr *> expr_pipe_from expr_pipe_into
%type <std::vector<Expr *>> list
%type <nix::ExprAttrs *> binds binds1
%type <nix::FormalsBuilder> formals formal_set
%type <nix::Formal> formal
%type <std::vector<nix::AttrName>> attrpath
%type <std::vector<std::pair<nix::AttrName, nix::PosIdx>>> attrs
%type <std::vector<std::pair<nix::PosIdx, nix::Expr *>>> string_parts_interpolated
%type <std::vector<std::pair<nix::PosIdx, std::variant<nix::Expr *, nix::StringToken>>>> ind_string_parts
%type <nix::Expr *> path_start
%type <std::variant<nix::Expr *, std::string_view>> string_parts string_attr
%type <nix::StringToken> attr
%token <nix::StringToken> ID
%token <nix::StringToken> STR IND_STR
%token <nix::NixInt> INT_LIT
%token <nix::NixFloat> FLOAT_LIT
%token <nix::StringToken> PATH HPATH SPATH PATH_END
%token <nix::StringToken> URI
%token IF THEN ELSE ASSERT WITH LET IN_KW REC INHERIT EQ NEQ AND OR IMPL OR_KW
%token PIPE_FROM PIPE_INTO /* <| and |> */
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

start: expr {
  state->result = $1;

  // This parser does not use yynerrs; suppress the warning.
  (void) yynerrs_;
};

expr: expr_function;

expr_function
  : ID ':' expr_function
    { auto me = new ExprLambda(CUR_POS, state->symbols.create($1), $3);
      $$ = me;
      SET_DOC_POS(me, @1);
    }
  | formal_set ':' expr_function[body]
    {
      state->validateFormals($formal_set);
      auto me = new ExprLambda(state->alloc, CUR_POS, std::move($formal_set), $body);
      $$ = me;
      SET_DOC_POS(me, @1);
    }
  | formal_set '@' ID ':' expr_function[body]
    {
      auto arg = state->symbols.create($ID);
      state->validateFormals($formal_set, CUR_POS, arg);
      auto me = new ExprLambda(state->alloc, CUR_POS, arg, std::move($formal_set), $body);
      $$ = me;
      SET_DOC_POS(me, @1);
    }
  | ID '@' formal_set ':' expr_function[body]
    {
      auto arg = state->symbols.create($ID);
      state->validateFormals($formal_set, CUR_POS, arg);
      auto me = new ExprLambda(state->alloc, CUR_POS, arg, std::move($formal_set), $body);
      $$ = me;
      SET_DOC_POS(me, @1);
    }
  | ASSERT expr ';' expr_function
    { $$ = new ExprAssert(CUR_POS, $2, $4); }
  | WITH expr ';' expr_function
    { $$ = new ExprWith(CUR_POS, $2, $4); }
  | LET binds IN_KW expr_function
    { if (!$2->dynamicAttrs.empty())
        throw ParseError({
            .msg = HintFmt("dynamic attributes not allowed in let"),
            .pos = state->positions[CUR_POS]
        });
      $$ = new ExprLet($2, $4);
    }
  | expr_if
  ;

expr_if
  : IF expr THEN expr ELSE expr { $$ = new ExprIf(CUR_POS, $2, $4, $6); }
  | expr_pipe_from
  | expr_pipe_into
  | expr_op
  ;

expr_pipe_from
  : expr_op PIPE_FROM expr_pipe_from { $$ = makeCall(state->at(@2), $1, $3); }
  | expr_op PIPE_FROM expr_op        { $$ = makeCall(state->at(@2), $1, $3); }
  ;

expr_pipe_into
  : expr_pipe_into PIPE_INTO expr_op { $$ = makeCall(state->at(@2), $3, $1); }
  | expr_op        PIPE_INTO expr_op { $$ = makeCall(state->at(@2), $3, $1); }
  ;

expr_op
  : '!' expr_op %prec NOT { $$ = new ExprOpNot($2); }
  | '-' expr_op %prec NEGATE { $$ = new ExprCall(CUR_POS, new ExprVar(state->s.sub), {new ExprInt(0), $2}); }
  | expr_op EQ expr_op { $$ = new ExprOpEq($1, $3); }
  | expr_op NEQ expr_op { $$ = new ExprOpNEq($1, $3); }
  | expr_op '<' expr_op { $$ = new ExprCall(state->at(@2), new ExprVar(state->s.lessThan), {$1, $3}); }
  | expr_op LEQ expr_op { $$ = new ExprOpNot(new ExprCall(state->at(@2), new ExprVar(state->s.lessThan), {$3, $1})); }
  | expr_op '>' expr_op { $$ = new ExprCall(state->at(@2), new ExprVar(state->s.lessThan), {$3, $1}); }
  | expr_op GEQ expr_op { $$ = new ExprOpNot(new ExprCall(state->at(@2), new ExprVar(state->s.lessThan), {$1, $3})); }
  | expr_op AND expr_op { $$ = new ExprOpAnd(state->at(@2), $1, $3); }
  | expr_op OR expr_op { $$ = new ExprOpOr(state->at(@2), $1, $3); }
  | expr_op IMPL expr_op { $$ = new ExprOpImpl(state->at(@2), $1, $3); }
  | expr_op UPDATE expr_op { $$ = new ExprOpUpdate(state->at(@2), $1, $3); }
  | expr_op '?' attrpath { $$ = new ExprOpHasAttr(state->alloc, $1, std::move($3)); }
  | expr_op '+' expr_op
    { $$ = new ExprConcatStrings(state->at(@2), false, {{state->at(@1), $1}, {state->at(@3), $3}}); }
  | expr_op '-' expr_op { $$ = new ExprCall(state->at(@2), new ExprVar(state->s.sub), {$1, $3}); }
  | expr_op '*' expr_op { $$ = new ExprCall(state->at(@2), new ExprVar(state->s.mul), {$1, $3}); }
  | expr_op '/' expr_op { $$ = new ExprCall(state->at(@2), new ExprVar(state->s.div), {$1, $3}); }
  | expr_op CONCAT expr_op { $$ = new ExprOpConcatLists(state->at(@2), $1, $3); }
  | expr_app
  ;

expr_app
  : expr_app expr_select { $$ = makeCall(CUR_POS, $1, $2); $2->warnIfCursedOr(state->symbols, state->positions); }
  | /* Once a ‘cursed or’ reaches this nonterminal, it is no longer cursed,
       because the uncursed parse would also produce an expr_app. But we need
       to remove the cursed status in order to prevent valid things like
       `f (g or)` from triggering the warning. */
    expr_select { $$ = $1; $$->resetCursedOr(); }
  ;

expr_select
  : expr_simple '.' attrpath
    { $$ = new ExprSelect(state->alloc, CUR_POS, $1, std::move($3), nullptr); }
  | expr_simple '.' attrpath OR_KW expr_select
    { $$ = new ExprSelect(state->alloc, CUR_POS, $1, std::move($3), $5); $5->warnIfCursedOr(state->symbols, state->positions); }
  | /* Backwards compatibility: because Nixpkgs has a function named ‘or’,
       allow stuff like ‘map or [...]’. This production is problematic (see
       https://github.com/NixOS/nix/issues/11118) and will be refactored in the
       future by treating `or` as a regular identifier. The refactor will (in
       very rare cases, we think) change the meaning of expressions, so we mark
       the ExprCall with data (establishing that it is a ‘cursed or’) that can
       be used to emit a warning when an affected expression is parsed. */
    expr_simple OR_KW
    { $$ = new ExprCall(CUR_POS, $1, {new ExprVar(CUR_POS, state->s.or_)}, state->positions.add(state->origin, @$.endOffset)); }
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
  | '"' string_parts '"' {
      std::visit(overloaded{
          [&](std::string_view str) { $$ = new ExprString(state->alloc, str); },
          [&](Expr * expr) { $$ = expr; }},
      $2);
  }
  | IND_STRING_OPEN ind_string_parts IND_STRING_CLOSE {
      $$ = state->stripIndentation(CUR_POS, std::move($2));
  }
  | path_start PATH_END
  | path_start string_parts_interpolated PATH_END {
      $2.insert($2.begin(), {state->at(@1), $1});
      $$ = new ExprConcatStrings(CUR_POS, false, std::move($2));
  }
  | SPATH {
      std::string_view path($1.p + 1, $1.l - 2);
      $$ = new ExprCall(CUR_POS,
          new ExprVar(state->s.findFile),
          {new ExprVar(state->s.nixPath),
           new ExprString(state->alloc, path)});
  }
  | URI {
      static bool noURLLiterals = experimentalFeatureSettings.isEnabled(Xp::NoUrlLiterals);
      if (noURLLiterals)
          throw ParseError({
              .msg = HintFmt("URL literals are disabled"),
              .pos = state->positions[CUR_POS]
          });
      $$ = new ExprString(state->alloc, $1);
  }
  | '(' expr ')' { $$ = $2; }
  /* Let expressions `let {..., body = ...}' are just desugared
     into `(rec {..., body = ...}).body'. */
  | LET '{' binds '}'
    { $3->recursive = true; $3->pos = CUR_POS; $$ = new ExprSelect(state->alloc, noPos, $3, state->s.body); }
  | REC '{' binds '}'
    { $3->recursive = true; $3->pos = CUR_POS; $$ = $3; }
  | '{' binds1 '}'
    { $2->pos = CUR_POS; $$ = $2; }
  | '{' '}'
    { $$ = new ExprAttrs(CUR_POS); }
  | '[' list ']' { $$ = new ExprList(state->alloc, std::move($2)); }
  ;

string_parts
  : STR { $$ = $1; }
  | string_parts_interpolated { $$ = new ExprConcatStrings(CUR_POS, true, std::move($1)); }
  | { $$ = std::string_view(); }
  ;

string_parts_interpolated
  : string_parts_interpolated STR
  { $$ = std::move($1); $$.emplace_back(state->at(@2), new ExprString(state->alloc, $2)); }
  | string_parts_interpolated DOLLAR_CURLY expr '}' { $$ = std::move($1); $$.emplace_back(state->at(@2), $3); }
  | DOLLAR_CURLY expr '}' { $$.emplace_back(state->at(@1), $2); }
  | STR DOLLAR_CURLY expr '}' {
      $$.emplace_back(state->at(@1), new ExprString(state->alloc, $1));
      $$.emplace_back(state->at(@2), $3);
    }
  ;

path_start
  : PATH {
    std::string_view literal({$1.p, $1.l});

    /* check for short path literals */
    if (state->settings.warnShortPathLiterals && literal.front() != '/' && literal.front() != '.') {
        logWarning({
            .msg = HintFmt("relative path literal '%s' should be prefixed with '.' for clarity: './%s'. (" ANSI_BOLD "warn-short-path-literals" ANSI_NORMAL " = true)", literal, literal),
            .pos = state->positions[CUR_POS]
        });
    }

    Path path(absPath(literal, state->basePath.path.abs()));
    /* add back in the trailing '/' to the first segment */
    if (literal.size() > 1 && literal.back() == '/')
      path += '/';
    $$ =
        /* Absolute paths are always interpreted relative to the
           root filesystem accessor, rather than the accessor of the
           current Nix expression. */
        literal.front() == '/'
        ? new ExprPath(state->alloc, state->rootFS, path)
        : new ExprPath(state->alloc, state->basePath.accessor, path);
  }
  | HPATH {
    if (state->settings.pureEval) {
        throw Error(
            "the path '%s' can not be resolved in pure mode",
            std::string_view($1.p, $1.l)
        );
    }
    Path path(getHome() + std::string($1.p + 1, $1.l - 1));
    $$ = new ExprPath(state->alloc, ref<SourceAccessor>(state->rootFS), path);
  }
  ;

ind_string_parts
  : ind_string_parts IND_STR { $$ = std::move($1); $$.emplace_back(state->at(@2), $2); }
  | ind_string_parts DOLLAR_CURLY expr '}' { $$ = std::move($1); $$.emplace_back(state->at(@2), $3); }
  | { }
  ;

binds
  : binds1
  | { $$ = new ExprAttrs; }
  ;

binds1
  : binds1[accum] attrpath '=' expr ';'
    { $$ = $accum;
      state->addAttr($$, std::move($attrpath), @attrpath, $expr, @expr);
    }
  | binds[accum] INHERIT attrs ';'
    { $$ = $accum;
      for (auto & [i, iPos] : $attrs) {
          if ($accum->attrs.find(i.symbol) != $accum->attrs.end())
              state->dupAttr(i.symbol, iPos, $accum->attrs[i.symbol].pos);
          $accum->attrs.emplace(
              i.symbol,
              ExprAttrs::AttrDef(new ExprVar(iPos, i.symbol), iPos, ExprAttrs::AttrDef::Kind::Inherited));
      }
    }
  | binds[accum] INHERIT '(' expr ')' attrs ';'
    { $$ = $accum;
      if (!$accum->inheritFromExprs)
          $accum->inheritFromExprs = std::make_unique<std::vector<Expr *>>();
      $accum->inheritFromExprs->push_back($expr);
      auto from = new nix::ExprInheritFrom(state->at(@expr), $accum->inheritFromExprs->size() - 1);
      for (auto & [i, iPos] : $attrs) {
          if ($accum->attrs.find(i.symbol) != $accum->attrs.end())
              state->dupAttr(i.symbol, iPos, $accum->attrs[i.symbol].pos);
          $accum->attrs.emplace(
              i.symbol,
              ExprAttrs::AttrDef(
                  new ExprSelect(state->alloc, iPos, from, i.symbol),
                  iPos,
                  ExprAttrs::AttrDef::Kind::InheritedFrom));
      }
    }
  | attrpath '=' expr ';'
    { $$ = new ExprAttrs;
      state->addAttr($$, std::move($attrpath), @attrpath, $expr, @expr);
    }
  ;

attrs
  : attrs attr { $$ = std::move($1); $$.emplace_back(state->symbols.create($2), state->at(@2)); }
  | attrs string_attr
    { $$ = std::move($1);
      std::visit(overloaded {
          [&](std::string_view str) { $$.emplace_back(state->symbols.create(str), state->at(@2)); },
          [&](Expr * expr) {
              throw ParseError({
                  .msg = HintFmt("dynamic attributes not allowed in inherit"),
                  .pos = state->positions[state->at(@2)]
              });
          }
      }, $2);
    }
  | { }
  ;

attrpath
  : attrpath '.' attr { $$ = std::move($1); $$.emplace_back(state->symbols.create($3)); }
  | attrpath '.' string_attr
    { $$ = std::move($1);
      std::visit(overloaded {
          [&](std::string_view str) { $$.emplace_back(state->symbols.create(str)); },
          [&](Expr * expr) { $$.emplace_back(expr); }
      }, std::move($3));
    }
  | attr { $$.emplace_back(state->symbols.create($1)); }
  | string_attr
    { std::visit(overloaded {
          [&](std::string_view str) { $$.emplace_back(state->symbols.create(str)); },
          [&](Expr * expr) { $$.emplace_back(expr); }
      }, std::move($1));
    }
  ;

attr
  : ID
  | OR_KW { $$ = {"or", 2}; }
  ;

string_attr
  : '"' string_parts '"' { $$ = std::move($2); }
  | DOLLAR_CURLY expr '}' { $$ = $2; }
  ;

list
  : list expr_select { $$ = std::move($1); $$.push_back($2); /* !!! dangerous */; $2->warnIfCursedOr(state->symbols, state->positions); }
  | { }
  ;

formal_set
  : '{' formals ',' ELLIPSIS '}' { $$ = std::move($formals); $$.ellipsis = true; }
  | '{' ELLIPSIS '}'                                       { $$.ellipsis = true; }
  | '{' formals ',' '}'          { $$ = std::move($formals); $$.ellipsis = false; }
  | '{' formals '}'              { $$ = std::move($formals); $$.ellipsis = false; }
  | '{' '}'                                                { $$.ellipsis = false; }
  ;

formals
  : formals[accum] ',' formal
    { $$ = std::move($accum); $$.formals.emplace_back(std::move($formal)); }
  | formal
    { $$.formals.emplace_back(std::move($formal)); }
  ;

formal
  : ID { $$ = Formal{CUR_POS, state->symbols.create($1), 0}; }
  | ID '?' expr { $$ = Formal{CUR_POS, state->symbols.create($1), $3}; }
  ;

%%

#include "nix/expr/eval.hh"


namespace nix {

Expr * parseExprFromBuf(
    char * text,
    size_t length,
    Pos::Origin origin,
    const SourcePath & basePath,
    std::pmr::polymorphic_allocator<char> & alloc,
    SymbolTable & symbols,
    const EvalSettings & settings,
    PosTable & positions,
    DocCommentMap & docComments,
    const ref<SourceAccessor> rootFS)
{
    yyscan_t scanner;
    LexerState lexerState {
        .positionToDocComment = docComments,
        .positions = positions,
        .origin = positions.addOrigin(origin, length),
    };
    ParserState state {
        .lexerState = lexerState,
        .alloc = alloc,
        .symbols = symbols,
        .positions = positions,
        .basePath = basePath,
        .origin = lexerState.origin,
        .rootFS = rootFS,
        .settings = settings,
    };

    yylex_init_extra(&lexerState, &scanner);
    Finally _destroy([&] { yylex_destroy(scanner); });

    yy_scan_buffer(text, length, scanner);
    Parser parser(scanner, &state);
    parser.parse();

    return state.result;
}


}
#pragma GCC diagnostic pop // end ignored "-Wswitch-enum"
