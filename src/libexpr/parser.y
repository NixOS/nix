%glr-parser
%define api.pure
%locations
%define parse.error verbose
%defines
/* %no-lines */
%parse-param { void * scanner }
%parse-param { nix::ParseData * data }
%lex-param { void * scanner }
%lex-param { nix::ParseData * data }
%expect 1
%expect-rr 1

%code requires {

#ifndef BISON_HEADER
#define BISON_HEADER

#include <variant>

#include "util.hh"

#include "nixexpr.hh"
#include "eval.hh"
#include "globals.hh"

namespace nix {

    struct ParseData
    {
        EvalState & state;
        SymbolTable & symbols;
        Expr * result;
        SourcePath basePath;
        PosTable::Origin origin;
        std::optional<ErrorInfo> error;
    };

    struct ParserFormals {
        std::vector<Formal> formals;
        bool ellipsis = false;
    };

}

// using C a struct allows us to avoid having to define the special
// members that using string_view here would implicitly delete.
struct StringToken {
  const char * p;
  size_t l;
  bool hasIndentation;
  operator std::string_view() const { return {p, l}; }
};

#define YY_DECL int yylex \
    (YYSTYPE * yylval_param, YYLTYPE * yylloc_param, yyscan_t yyscanner, nix::ParseData * data)

#endif

}

%{

#include "parser-tab.hh"
#include "lexer-tab.hh"

YY_DECL;

using namespace nix;


namespace nix {


static void dupAttr(const EvalState & state, const AttrPath & attrPath, const PosIdx pos, const PosIdx prevPos)
{
    throw ParseError({
         .msg = hintfmt("attribute '%1%' already defined at %2%",
             showAttrPath(state.symbols, attrPath), state.positions[prevPos]),
         .errPos = state.positions[pos]
    });
}

static void dupAttr(const EvalState & state, Symbol attr, const PosIdx pos, const PosIdx prevPos)
{
    throw ParseError({
        .msg = hintfmt("attribute '%1%' already defined at %2%", state.symbols[attr], state.positions[prevPos]),
        .errPos = state.positions[pos]
    });
}


static void addAttr(ExprAttrs * attrs, AttrPath & attrPath,
    Expr * e, const PosIdx pos, const nix::EvalState & state)
{
    AttrPath::iterator i;
    // All attrpaths have at least one attr
    assert(!attrPath.empty());
    // Checking attrPath validity.
    // ===========================
    for (i = attrPath.begin(); i + 1 < attrPath.end(); i++) {
        if (i->symbol) {
            ExprAttrs::AttrDefs::iterator j = attrs->attrs.find(i->symbol);
            if (j != attrs->attrs.end()) {
                if (!j->second.inherited) {
                    ExprAttrs * attrs2 = dynamic_cast<ExprAttrs *>(j->second.e);
                    if (!attrs2) dupAttr(state, attrPath, pos, j->second.pos);
                    attrs = attrs2;
                } else
                    dupAttr(state, attrPath, pos, j->second.pos);
            } else {
                ExprAttrs * nested = new ExprAttrs;
                attrs->attrs[i->symbol] = ExprAttrs::AttrDef(nested, pos);
                attrs = nested;
            }
        } else {
            ExprAttrs *nested = new ExprAttrs;
            attrs->dynamicAttrs.push_back(ExprAttrs::DynamicAttrDef(i->expr, nested, pos));
            attrs = nested;
        }
    }
    // Expr insertion.
    // ==========================
    if (i->symbol) {
        ExprAttrs::AttrDefs::iterator j = attrs->attrs.find(i->symbol);
        if (j != attrs->attrs.end()) {
            // This attr path is already defined. However, if both
            // e and the expr pointed by the attr path are two attribute sets,
            // we want to merge them.
            // Otherwise, throw an error.
            auto ae = dynamic_cast<ExprAttrs *>(e);
            auto jAttrs = dynamic_cast<ExprAttrs *>(j->second.e);
            if (jAttrs && ae) {
                for (auto & ad : ae->attrs) {
                    auto j2 = jAttrs->attrs.find(ad.first);
                    if (j2 != jAttrs->attrs.end()) // Attr already defined in iAttrs, error.
                        dupAttr(state, ad.first, j2->second.pos, ad.second.pos);
                    jAttrs->attrs.emplace(ad.first, ad.second);
                }
            } else {
                dupAttr(state, attrPath, pos, j->second.pos);
            }
        } else {
            // This attr path is not defined. Let's create it.
            attrs->attrs.emplace(i->symbol, ExprAttrs::AttrDef(e, pos));
            e->setName(i->symbol);
        }
    } else {
        attrs->dynamicAttrs.push_back(ExprAttrs::DynamicAttrDef(i->expr, e, pos));
    }
}


static Formals * toFormals(ParseData & data, ParserFormals * formals,
    PosIdx pos = noPos, Symbol arg = {})
{
    std::sort(formals->formals.begin(), formals->formals.end(),
        [] (const auto & a, const auto & b) {
            return std::tie(a.name, a.pos) < std::tie(b.name, b.pos);
        });

    std::optional<std::pair<Symbol, PosIdx>> duplicate;
    for (size_t i = 0; i + 1 < formals->formals.size(); i++) {
        if (formals->formals[i].name != formals->formals[i + 1].name)
            continue;
        std::pair thisDup{formals->formals[i].name, formals->formals[i + 1].pos};
        duplicate = std::min(thisDup, duplicate.value_or(thisDup));
    }
    if (duplicate)
        throw ParseError({
            .msg = hintfmt("duplicate formal function argument '%1%'", data.symbols[duplicate->first]),
            .errPos = data.state.positions[duplicate->second]
        });

    Formals result;
    result.ellipsis = formals->ellipsis;
    result.formals = std::move(formals->formals);

    if (arg && result.has(arg))
        throw ParseError({
            .msg = hintfmt("duplicate formal function argument '%1%'", data.symbols[arg]),
            .errPos = data.state.positions[pos]
        });

    delete formals;
    return new Formals(std::move(result));
}


static Expr * stripIndentation(const PosIdx pos, SymbolTable & symbols,
    std::vector<std::pair<PosIdx, std::variant<Expr *, StringToken>>> & es)
{
    if (es.empty()) return new ExprString("");

    /* Figure out the minimum indentation.  Note that by design
       whitespace-only final lines are not taken into account.  (So
       the " " in "\n ''" is ignored, but the " " in "\n foo''" is.) */
    bool atStartOfLine = true; /* = seen only whitespace in the current line */
    size_t minIndent = 1000000;
    size_t curIndent = 0;
    for (auto & [i_pos, i] : es) {
        auto * str = std::get_if<StringToken>(&i);
        if (!str || !str->hasIndentation) {
            /* Anti-quotations and escaped characters end the current start-of-line whitespace. */
            if (atStartOfLine) {
                atStartOfLine = false;
                if (curIndent < minIndent) minIndent = curIndent;
            }
            continue;
        }
        for (size_t j = 0; j < str->l; ++j) {
            if (atStartOfLine) {
                if (str->p[j] == ' ')
                    curIndent++;
                else if (str->p[j] == '\n') {
                    /* Empty line, doesn't influence minimum
                       indentation. */
                    curIndent = 0;
                } else {
                    atStartOfLine = false;
                    if (curIndent < minIndent) minIndent = curIndent;
                }
            } else if (str->p[j] == '\n') {
                atStartOfLine = true;
                curIndent = 0;
            }
        }
    }

    /* Strip spaces from each line. */
    auto * es2 = new std::vector<std::pair<PosIdx, Expr *>>;
    atStartOfLine = true;
    size_t curDropped = 0;
    size_t n = es.size();
    auto i = es.begin();
    const auto trimExpr = [&] (Expr * e) {
        atStartOfLine = false;
        curDropped = 0;
        es2->emplace_back(i->first, e);
    };
    const auto trimString = [&] (const StringToken & t) {
        std::string s2;
        for (size_t j = 0; j < t.l; ++j) {
            if (atStartOfLine) {
                if (t.p[j] == ' ') {
                    if (curDropped++ >= minIndent)
                        s2 += t.p[j];
                }
                else if (t.p[j] == '\n') {
                    curDropped = 0;
                    s2 += t.p[j];
                } else {
                    atStartOfLine = false;
                    curDropped = 0;
                    s2 += t.p[j];
                }
            } else {
                s2 += t.p[j];
                if (t.p[j] == '\n') atStartOfLine = true;
            }
        }

        /* Remove the last line if it is empty and consists only of
           spaces. */
        if (n == 1) {
            std::string::size_type p = s2.find_last_of('\n');
            if (p != std::string::npos && s2.find_first_not_of(' ', p + 1) == std::string::npos)
                s2 = std::string(s2, 0, p + 1);
        }

        es2->emplace_back(i->first, new ExprString(s2));
    };
    for (; i != es.end(); ++i, --n) {
        std::visit(overloaded { trimExpr, trimString }, i->second);
    }

    /* If this is a single string, then don't do a concatenation. */
    return es2->size() == 1 && dynamic_cast<ExprString *>((*es2)[0].second) ? (*es2)[0].second : new ExprConcatStrings(pos, true, es2);
}


static inline PosIdx makeCurPos(const YYLTYPE & loc, ParseData * data)
{
    return data->state.positions.add(data->origin, loc.first_line, loc.first_column);
}

#define CUR_POS makeCurPos(*yylocp, data)


}


void yyerror(YYLTYPE * loc, yyscan_t scanner, ParseData * data, const char * error)
{
    data->error = {
        .msg = hintfmt(error),
        .errPos = data->state.positions[makeCurPos(*loc, data)]
    };
}


%}

%union {
  // !!! We're probably leaking stuff here.
  nix::Expr * e;
  nix::ExprList * list;
  nix::ExprAttrs * attrs;
  nix::ParserFormals * formals;
  nix::Formal * formal;
  nix::NixInt n;
  nix::NixFloat nf;
  StringToken id; // !!! -> Symbol
  StringToken path;
  StringToken uri;
  StringToken str;
  std::vector<nix::AttrName> * attrNames;
  std::vector<std::pair<nix::PosIdx, nix::Expr *>> * string_parts;
  std::vector<std::pair<nix::PosIdx, std::variant<nix::Expr *, StringToken>>> * ind_string_parts;
}

%type <e> start expr expr_function expr_if expr_op
%type <e> expr_select expr_simple expr_app
%type <list> expr_list
%type <attrs> binds
%type <formals> formals
%type <formal> formal
%type <attrNames> attrs attrpath
%type <string_parts> string_parts_interpolated
%type <ind_string_parts> ind_string_parts
%type <e> path_start string_parts string_attr
%type <id> attr
%token <id> ID ATTRPATH
%token <str> STR IND_STR
%token <n> INT
%token <nf> FLOAT
%token <path> PATH HPATH SPATH PATH_END
%token <uri> URI
%token IF THEN ELSE ASSERT WITH LET IN REC INHERIT EQ NEQ AND OR IMPL OR_KW
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

start: expr { data->result = $1; };

expr: expr_function;

expr_function
  : ID ':' expr_function
    { $$ = new ExprLambda(CUR_POS, data->symbols.create($1), 0, $3); }
  | '{' formals '}' ':' expr_function
    { $$ = new ExprLambda(CUR_POS, toFormals(*data, $2), $5); }
  | '{' formals '}' '@' ID ':' expr_function
    {
      auto arg = data->symbols.create($5);
      $$ = new ExprLambda(CUR_POS, arg, toFormals(*data, $2, CUR_POS, arg), $7);
    }
  | ID '@' '{' formals '}' ':' expr_function
    {
      auto arg = data->symbols.create($1);
      $$ = new ExprLambda(CUR_POS, arg, toFormals(*data, $4, CUR_POS, arg), $7);
    }
  | ASSERT expr ';' expr_function
    { $$ = new ExprAssert(CUR_POS, $2, $4); }
  | WITH expr ';' expr_function
    { $$ = new ExprWith(CUR_POS, $2, $4); }
  | LET binds IN expr_function
    { if (!$2->dynamicAttrs.empty())
        throw ParseError({
            .msg = hintfmt("dynamic attributes not allowed in let"),
            .errPos = data->state.positions[CUR_POS]
        });
      $$ = new ExprLet($2, $4);
    }
  | expr_if
  ;

expr_if
  : IF expr THEN expr ELSE expr { $$ = new ExprIf(CUR_POS, $2, $4, $6); }
  | expr_op
  ;

expr_op
  : '!' expr_op %prec NOT { $$ = new ExprOpNot($2); }
  | '-' expr_op %prec NEGATE { $$ = new ExprCall(CUR_POS, new ExprVar(data->symbols.create("__sub")), {new ExprInt(0), $2}); }
  | expr_op EQ expr_op { $$ = new ExprOpEq($1, $3); }
  | expr_op NEQ expr_op { $$ = new ExprOpNEq($1, $3); }
  | expr_op '<' expr_op { $$ = new ExprCall(CUR_POS, new ExprVar(data->symbols.create("__lessThan")), {$1, $3}); }
  | expr_op LEQ expr_op { $$ = new ExprOpNot(new ExprCall(CUR_POS, new ExprVar(data->symbols.create("__lessThan")), {$3, $1})); }
  | expr_op '>' expr_op { $$ = new ExprCall(CUR_POS, new ExprVar(data->symbols.create("__lessThan")), {$3, $1}); }
  | expr_op GEQ expr_op { $$ = new ExprOpNot(new ExprCall(CUR_POS, new ExprVar(data->symbols.create("__lessThan")), {$1, $3})); }
  | expr_op AND expr_op { $$ = new ExprOpAnd(CUR_POS, $1, $3); }
  | expr_op OR expr_op { $$ = new ExprOpOr(CUR_POS, $1, $3); }
  | expr_op IMPL expr_op { $$ = new ExprOpImpl(CUR_POS, $1, $3); }
  | expr_op UPDATE expr_op { $$ = new ExprOpUpdate(CUR_POS, $1, $3); }
  | expr_op '?' attrpath { $$ = new ExprOpHasAttr($1, *$3); }
  | expr_op '+' expr_op
    { $$ = new ExprConcatStrings(CUR_POS, false, new std::vector<std::pair<PosIdx, Expr *>>({{makeCurPos(@1, data), $1}, {makeCurPos(@3, data), $3}})); }
  | expr_op '-' expr_op { $$ = new ExprCall(CUR_POS, new ExprVar(data->symbols.create("__sub")), {$1, $3}); }
  | expr_op '*' expr_op { $$ = new ExprCall(CUR_POS, new ExprVar(data->symbols.create("__mul")), {$1, $3}); }
  | expr_op '/' expr_op { $$ = new ExprCall(CUR_POS, new ExprVar(data->symbols.create("__div")), {$1, $3}); }
  | expr_op CONCAT expr_op { $$ = new ExprOpConcatLists(CUR_POS, $1, $3); }
  | expr_app
  ;

expr_app
  : expr_app expr_select {
      if (auto e2 = dynamic_cast<ExprCall *>($1)) {
          e2->args.push_back($2);
          $$ = $1;
      } else
          $$ = new ExprCall(CUR_POS, $1, {$2});
  }
  | expr_select
  ;

expr_select
  : expr_simple '.' attrpath
    { $$ = new ExprSelect(CUR_POS, $1, *$3, 0); }
  | expr_simple '.' attrpath OR_KW expr_select
    { $$ = new ExprSelect(CUR_POS, $1, *$3, $5); }
  | /* Backwards compatibility: because Nixpkgs has a rarely used
       function named ‘or’, allow stuff like ‘map or [...]’. */
    expr_simple OR_KW
    { $$ = new ExprCall(CUR_POS, $1, {new ExprVar(CUR_POS, data->symbols.create("or"))}); }
  | expr_simple { $$ = $1; }
  ;

expr_simple
  : ID {
      std::string_view s = "__curPos";
      if ($1.l == s.size() && strncmp($1.p, s.data(), s.size()) == 0)
          $$ = new ExprPos(CUR_POS);
      else
          $$ = new ExprVar(CUR_POS, data->symbols.create($1));
  }
  | INT { $$ = new ExprInt($1); }
  | FLOAT { $$ = new ExprFloat($1); }
  | '"' string_parts '"' { $$ = $2; }
  | IND_STRING_OPEN ind_string_parts IND_STRING_CLOSE {
      $$ = stripIndentation(CUR_POS, data->symbols, *$2);
  }
  | path_start PATH_END { $$ = $1; }
  | path_start string_parts_interpolated PATH_END {
      $2->insert($2->begin(), {makeCurPos(@1, data), $1});
      $$ = new ExprConcatStrings(CUR_POS, false, $2);
  }
  | SPATH {
      std::string path($1.p + 1, $1.l - 2);
      $$ = new ExprCall(CUR_POS,
          new ExprVar(data->symbols.create("__findFile")),
          {new ExprVar(data->symbols.create("__nixPath")),
           new ExprString(path)});
  }
  | URI {
      static bool noURLLiterals = settings.isExperimentalFeatureEnabled(Xp::NoUrlLiterals);
      if (noURLLiterals)
          throw ParseError({
              .msg = hintfmt("URL literals are disabled"),
              .errPos = data->state.positions[CUR_POS]
          });
      $$ = new ExprString(std::string($1));
  }
  | '(' expr ')' { $$ = $2; }
  /* Let expressions `let {..., body = ...}' are just desugared
     into `(rec {..., body = ...}).body'. */
  | LET '{' binds '}'
    { $3->recursive = true; $$ = new ExprSelect(noPos, $3, data->symbols.create("body")); }
  | REC '{' binds '}'
    { $3->recursive = true; $$ = $3; }
  | '{' binds '}'
    { $$ = $2; }
  | '[' expr_list ']' { $$ = $2; }
  ;

string_parts
  : STR { $$ = new ExprString(std::string($1)); }
  | string_parts_interpolated { $$ = new ExprConcatStrings(CUR_POS, true, $1); }
  | { $$ = new ExprString(""); }
  ;

string_parts_interpolated
  : string_parts_interpolated STR
  { $$ = $1; $1->emplace_back(makeCurPos(@2, data), new ExprString(std::string($2))); }
  | string_parts_interpolated DOLLAR_CURLY expr '}' { $$ = $1; $1->emplace_back(makeCurPos(@2, data), $3); }
  | DOLLAR_CURLY expr '}' { $$ = new std::vector<std::pair<PosIdx, Expr *>>; $$->emplace_back(makeCurPos(@1, data), $2); }
  | STR DOLLAR_CURLY expr '}' {
      $$ = new std::vector<std::pair<PosIdx, Expr *>>;
      $$->emplace_back(makeCurPos(@1, data), new ExprString(std::string($1)));
      $$->emplace_back(makeCurPos(@2, data), $3);
    }
  ;

path_start
  : PATH {
      std::string_view path({$1.p, $1.l});
      $$ = new ExprPath(
          /* Absolute paths are always interpreted relative to the
             root filesystem accessor, rather than the accessor of the
             current Nix expression. */
          hasPrefix(path, "/")
          ? SourcePath{data->state.rootFS, CanonPath(path)}
          : SourcePath{data->basePath.accessor, CanonPath(path, data->basePath.path)});
  }
  | HPATH {
    if (evalSettings.pureEval) {
        throw Error(
            "the path '%s' can not be resolved in pure mode",
            std::string_view($1.p, $1.l)
        );
    }
    Path path(getHome() + std::string($1.p + 1, $1.l - 1));
    $$ = new ExprPath(data->state.rootPath(path));
  }
  ;

ind_string_parts
  : ind_string_parts IND_STR { $$ = $1; $1->emplace_back(makeCurPos(@2, data), $2); }
  | ind_string_parts DOLLAR_CURLY expr '}' { $$ = $1; $1->emplace_back(makeCurPos(@2, data), $3); }
  | { $$ = new std::vector<std::pair<PosIdx, std::variant<Expr *, StringToken>>>; }
  ;

binds
  : binds attrpath '=' expr ';' { $$ = $1; addAttr($$, *$2, $4, makeCurPos(@2, data), data->state); }
  | binds INHERIT attrs ';'
    { $$ = $1;
      for (auto & i : *$3) {
          if ($$->attrs.find(i.symbol) != $$->attrs.end())
              dupAttr(data->state, i.symbol, makeCurPos(@3, data), $$->attrs[i.symbol].pos);
          auto pos = makeCurPos(@3, data);
          $$->attrs.emplace(i.symbol, ExprAttrs::AttrDef(new ExprVar(CUR_POS, i.symbol), pos, true));
      }
    }
  | binds INHERIT '(' expr ')' attrs ';'
    { $$ = $1;
      /* !!! Should ensure sharing of the expression in $4. */
      for (auto & i : *$6) {
          if ($$->attrs.find(i.symbol) != $$->attrs.end())
              dupAttr(data->state, i.symbol, makeCurPos(@6, data), $$->attrs[i.symbol].pos);
          $$->attrs.emplace(i.symbol, ExprAttrs::AttrDef(new ExprSelect(CUR_POS, $4, i.symbol), makeCurPos(@6, data)));
      }
    }
  | { $$ = new ExprAttrs(makeCurPos(@0, data)); }
  ;

attrs
  : attrs attr { $$ = $1; $1->push_back(AttrName(data->symbols.create($2))); }
  | attrs string_attr
    { $$ = $1;
      ExprString * str = dynamic_cast<ExprString *>($2);
      if (str) {
          $$->push_back(AttrName(data->symbols.create(str->s)));
          delete str;
      } else
          throw ParseError({
              .msg = hintfmt("dynamic attributes not allowed in inherit"),
              .errPos = data->state.positions[makeCurPos(@2, data)]
          });
    }
  | { $$ = new AttrPath; }
  ;

attrpath
  : attrpath '.' attr { $$ = $1; $1->push_back(AttrName(data->symbols.create($3))); }
  | attrpath '.' string_attr
    { $$ = $1;
      ExprString * str = dynamic_cast<ExprString *>($3);
      if (str) {
          $$->push_back(AttrName(data->symbols.create(str->s)));
          delete str;
      } else
          $$->push_back(AttrName($3));
    }
  | attr { $$ = new std::vector<AttrName>; $$->push_back(AttrName(data->symbols.create($1))); }
  | string_attr
    { $$ = new std::vector<AttrName>;
      ExprString *str = dynamic_cast<ExprString *>($1);
      if (str) {
          $$->push_back(AttrName(data->symbols.create(str->s)));
          delete str;
      } else
          $$->push_back(AttrName($1));
    }
  ;

attr
  : ID { $$ = $1; }
  | OR_KW { $$ = {"or", 2}; }
  ;

string_attr
  : '"' string_parts '"' { $$ = $2; }
  | DOLLAR_CURLY expr '}' { $$ = $2; }
  ;

expr_list
  : expr_list expr_select { $$ = $1; $1->elems.push_back($2); /* !!! dangerous */ }
  | { $$ = new ExprList; }
  ;

formals
  : formal ',' formals
    { $$ = $3; $$->formals.push_back(*$1); }
  | formal
    { $$ = new ParserFormals; $$->formals.push_back(*$1); $$->ellipsis = false; }
  |
    { $$ = new ParserFormals; $$->ellipsis = false; }
  | ELLIPSIS
    { $$ = new ParserFormals; $$->ellipsis = true; }
  ;

formal
  : ID { $$ = new Formal{CUR_POS, data->symbols.create($1), 0}; }
  | ID '?' expr { $$ = new Formal{CUR_POS, data->symbols.create($1), $3}; }
  ;

%%


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "eval.hh"
#include "filetransfer.hh"
#include "fetchers.hh"
#include "fs-input-accessor.hh"
#include "tarball.hh"
#include "store-api.hh"
#include "flake/flake.hh"


namespace nix {


Expr * EvalState::parse(
    char * text,
    size_t length,
    Pos::Origin origin,
    const SourcePath & basePath,
    std::shared_ptr<StaticEnv> & staticEnv)
{
    yyscan_t scanner;
    ParseData data {
        .state = *this,
        .symbols = symbols,
        .basePath = basePath,
        .origin = {origin},
    };

    yylex_init(&scanner);
    yy_scan_buffer(text, length, scanner);
    int res = yyparse(scanner, &data);
    yylex_destroy(scanner);

    if (res) throw ParseError(data.error.value());

    data.result->bindVars(*this, staticEnv);

    return data.result;
}


SourcePath resolveExprPath(const SourcePath & path)
{
    /* If `path' is a symlink, follow it.  This is so that relative
       path references work. */
    auto path2 = path.resolveSymlinks();

    /* If `path' refers to a directory, append `/default.nix'. */
    if (path2.lstat().type == InputAccessor::tDirectory)
        return path2 + "default.nix";

    return path2;
}


Expr * EvalState::parseExprFromFile(const SourcePath & path)
{
    return parseExprFromFile(path, staticBaseEnv);
}


Expr * EvalState::parseExprFromFile(const SourcePath & path, std::shared_ptr<StaticEnv> & staticEnv)
{
    auto buffer = path.readFile();
    // readFile hopefully have left some extra space for terminators
    buffer.append("\0\0", 2);
    return parse(buffer.data(), buffer.size(), Pos::Origin(path), path.parent(), staticEnv);
}


Expr * EvalState::parseExprFromString(std::string s, const SourcePath & basePath, std::shared_ptr<StaticEnv> & staticEnv)
{
    s.append("\0\0", 2);
    return parse(s.data(), s.size(), Pos::string_tag(), basePath, staticEnv);
}


Expr * EvalState::parseExprFromString(std::string s, const SourcePath & basePath)
{
    return parseExprFromString(std::move(s), basePath, staticBaseEnv);
}


Expr * EvalState::parseStdin()
{
    //Activity act(*logger, lvlTalkative, format("parsing standard input"));
    auto buffer = drainFD(0);
    // drainFD should have left some extra space for terminators
    buffer.append("\0\0", 2);
    return parse(buffer.data(), buffer.size(), Pos::stdin_tag(), rootPath(absPath(".")), staticBaseEnv);
}


void EvalState::addToSearchPath(const std::string & s)
{
    size_t pos = s.find('=');
    std::string prefix;
    Path path;
    if (pos == std::string::npos) {
        path = s;
    } else {
        prefix = std::string(s, 0, pos);
        path = std::string(s, pos + 1);
    }

    searchPath.emplace_back(prefix, path);
}


SourcePath EvalState::findFile(const std::string_view path)
{
    return findFile(searchPath, path);
}


SourcePath EvalState::findFile(SearchPath & searchPath, const std::string_view path, const PosIdx pos)
{
    for (auto & i : searchPath) {
        std::string suffix;
        if (i.first.empty())
            suffix = concatStrings("/", path);
        else {
            auto s = i.first.size();
            if (path.compare(0, s, i.first) != 0 ||
                (path.size() > s && path[s] != '/'))
                continue;
            suffix = path.size() == s ? "" : concatStrings("/", path.substr(s));
        }
        if (auto path = resolveSearchPathElem(i)) {
            auto res = *path + CanonPath(suffix);
            if (res.pathExists()) return res;
        }
    }

    if (hasPrefix(path, "nix/"))
        return {corepkgsFS, CanonPath(path.substr(3))};

    debugThrowLastTrace(ThrownError({
        .msg = hintfmt(evalSettings.pureEval
            ? "cannot look up '<%s>' in pure evaluation mode (use '--impure' to override)"
            : "file '%s' was not found in the Nix search path (add it using $NIX_PATH or -I)",
            path),
        .errPos = positions[pos]
    }));
}


std::optional<SourcePath> EvalState::resolveSearchPathElem(const SearchPathElem & elem, bool initAccessControl)
{
    auto i = searchPathResolved.find(elem.second);
    if (i != searchPathResolved.end()) return i->second;

    std::optional<SourcePath> res;

    if (EvalSettings::isPseudoUrl(elem.second)) {
        try {
            auto storePath = fetchers::downloadTarball(
                store, EvalSettings::resolvePseudoUrl(elem.second), "source", false).first;
            auto accessor = makeStorePathAccessor(store, storePath);
            registerAccessor(accessor);
            res.emplace(accessor->root());
        } catch (FileTransferError & e) {
            logWarning({
                .msg = hintfmt("Nix search path entry '%1%' cannot be downloaded, ignoring", elem.second)
            });
        }
    }

    else if (hasPrefix(elem.second, "flake:")) {
        settings.requireExperimentalFeature(Xp::Flakes);
        auto flakeRef = parseFlakeRef(elem.second.substr(6), {}, true, false);
        debug("fetching flake search path element '%s''", elem.second);
        auto [accessor, _] = flakeRef.resolve(store).lazyFetch(store);
        res.emplace(accessor->root());
    }

    else {
        auto path = rootPath(absPath(elem.second));

        /* Allow access to paths in the search path. */
        if (initAccessControl) {
            allowPath(path.path.abs());
            if (store->isInStore(path.path.abs())) {
                try {
                    StorePathSet closure;
                    store->computeFSClosure(store->toStorePath(path.path.abs()).first, closure);
                    for (auto & p : closure)
                        allowPath(p);
                } catch (InvalidPath &) { }
            }
        }

        if (path.pathExists())
            res.emplace(path);
        else {
            logWarning({
                .msg = hintfmt("Nix search path entry '%1%' does not exist, ignoring", elem.second)
            });
        }
    }

    if (res)
        debug("resolved search path element '%s' to '%s'", elem.second, *res);

    searchPathResolved.emplace(elem.second, res);
    return res;
}


}
