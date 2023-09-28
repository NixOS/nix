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
#include "parser/requires.hh"
}
%{
#include "parser/prologue.inc"
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
%token <id> ID
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
  | expr_op '<' expr_op { $$ = new ExprCall(makeCurPos(@2, data), new ExprVar(data->symbols.create("__lessThan")), {$1, $3}); }
  | expr_op LEQ expr_op { $$ = new ExprOpNot(new ExprCall(makeCurPos(@2, data), new ExprVar(data->symbols.create("__lessThan")), {$3, $1})); }
  | expr_op '>' expr_op { $$ = new ExprCall(makeCurPos(@2, data), new ExprVar(data->symbols.create("__lessThan")), {$3, $1}); }
  | expr_op GEQ expr_op { $$ = new ExprOpNot(new ExprCall(makeCurPos(@2, data), new ExprVar(data->symbols.create("__lessThan")), {$1, $3})); }
  | expr_op AND expr_op { $$ = new ExprOpAnd(makeCurPos(@2, data), $1, $3); }
  | expr_op OR expr_op { $$ = new ExprOpOr(makeCurPos(@2, data), $1, $3); }
  | expr_op IMPL expr_op { $$ = new ExprOpImpl(makeCurPos(@2, data), $1, $3); }
  | expr_op UPDATE expr_op { $$ = new ExprOpUpdate(makeCurPos(@2, data), $1, $3); }
  | expr_op '?' attrpath { $$ = new ExprOpHasAttr($1, std::move(*$3)); delete $3; }
  | expr_op '+' expr_op
    { $$ = new ExprConcatStrings(makeCurPos(@2, data), false, new std::vector<std::pair<PosIdx, Expr *> >({{makeCurPos(@1, data), $1}, {makeCurPos(@3, data), $3}})); }
  | expr_op '-' expr_op { $$ = new ExprCall(makeCurPos(@2, data), new ExprVar(data->symbols.create("__sub")), {$1, $3}); }
  | expr_op '*' expr_op { $$ = new ExprCall(makeCurPos(@2, data), new ExprVar(data->symbols.create("__mul")), {$1, $3}); }
  | expr_op '/' expr_op { $$ = new ExprCall(makeCurPos(@2, data), new ExprVar(data->symbols.create("__div")), {$1, $3}); }
  | expr_op CONCAT expr_op { $$ = new ExprOpConcatLists(makeCurPos(@2, data), $1, $3); }
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
    { $$ = new ExprSelect(CUR_POS, $1, std::move(*$3), nullptr); delete $3; }
  | expr_simple '.' attrpath OR_KW expr_select
    { $$ = new ExprSelect(CUR_POS, $1, std::move(*$3), $5); delete $3; }
  | /* Backwards compatibility: because Nixpkgs has a rarely used
       function named ‘or’, allow stuff like ‘map or [...]’. */
    expr_simple OR_KW
    { $$ = new ExprCall(CUR_POS, $1, {new ExprVar(CUR_POS, data->symbols.create("or"))}); }
  | expr_simple
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
      $$ = stripIndentation(CUR_POS, data->symbols, std::move(*$2));
      delete $2;
  }
  | path_start PATH_END
  | path_start string_parts_interpolated PATH_END {
      $2->insert($2->begin(), {makeCurPos(@1, data), $1});
      $$ = new ExprConcatStrings(CUR_POS, false, $2);
  }
  | SPATH {
      std::string path($1.p + 1, $1.l - 2);
      $$ = new ExprCall(CUR_POS,
          new ExprVar(data->symbols.create("__findFile")),
          {new ExprVar(data->symbols.create("__nixPath")),
           new ExprString(std::move(path))});
  }
  | URI {
      static bool noURLLiterals = experimentalFeatureSettings.isEnabled(Xp::NoUrlLiterals);
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
    Path path(absPath({$1.p, $1.l}, data->basePath.path.abs()));
    /* add back in the trailing '/' to the first segment */
    if ($1.p[$1.l-1] == '/' && $1.l > 1)
      path += "/";
    $$ = new ExprPath(std::move(path));
  }
  | HPATH {
    if (evalSettings.pureEval) {
        throw Error(
            "the path '%s' can not be resolved in pure mode",
            std::string_view($1.p, $1.l)
        );
    }
    Path path(getHome() + std::string($1.p + 1, $1.l - 1));
    $$ = new ExprPath(std::move(path));
  }
  ;

ind_string_parts
  : ind_string_parts IND_STR { $$ = $1; $1->emplace_back(makeCurPos(@2, data), $2); }
  | ind_string_parts DOLLAR_CURLY expr '}' { $$ = $1; $1->emplace_back(makeCurPos(@2, data), $3); }
  | { $$ = new std::vector<std::pair<PosIdx, std::variant<Expr *, StringToken>>>; }
  ;

binds
  : binds attrpath '=' expr ';' { $$ = $1; addAttr($$, std::move(*$2), $4, makeCurPos(@2, data), data->state); delete $2; }
  | binds INHERIT attrs ';'
    { $$ = $1;
      for (auto & i : *$3) {
          if ($$->attrs.find(i.symbol) != $$->attrs.end())
              dupAttr(data->state, i.symbol, makeCurPos(@3, data), $$->attrs[i.symbol].pos);
          auto pos = makeCurPos(@3, data);
          $$->attrs.emplace(i.symbol, ExprAttrs::AttrDef(new ExprVar(CUR_POS, i.symbol), pos, true));
      }
      delete $3;
    }
  | binds INHERIT '(' expr ')' attrs ';'
    { $$ = $1;
      /* !!! Should ensure sharing of the expression in $4. */
      for (auto & i : *$6) {
          if ($$->attrs.find(i.symbol) != $$->attrs.end())
              dupAttr(data->state, i.symbol, makeCurPos(@6, data), $$->attrs[i.symbol].pos);
          $$->attrs.emplace(i.symbol, ExprAttrs::AttrDef(new ExprSelect(CUR_POS, $4, i.symbol), makeCurPos(@6, data)));
      }
      delete $6;
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
  : ID
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
    { $$ = $3; $$->formals.emplace_back(*$1); delete $1; }
  | formal
    { $$ = new ParserFormals; $$->formals.emplace_back(*$1); $$->ellipsis = false; delete $1; }
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

#include "parser/epilogue.inc"
