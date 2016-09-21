%glr-parser
%pure-parser
%locations
%error-verbose
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

#include "util.hh"

#include "nixexpr.hh"
#include "eval.hh"

namespace nix {

    struct ParseData
    {
        EvalState & state;
        SymbolTable & symbols;
        Expr * result;
        Path basePath;
        Symbol path;
        string error;
        bool atEnd;
        Symbol sLetBody;
        ParseData(EvalState & state)
            : state(state)
            , symbols(state.symbols)
            , atEnd(false)
            , sLetBody(symbols.create("<let-body>"))
            { };
    };

}

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


static void dupAttr(const AttrPath & attrPath, const Pos & pos, const Pos & prevPos)
{
    throw ParseError(format("attribute ‘%1%’ at %2% already defined at %3%")
        % showAttrPath(attrPath) % pos % prevPos);
}


static void dupAttr(Symbol attr, const Pos & pos, const Pos & prevPos)
{
    throw ParseError(format("attribute ‘%1%’ at %2% already defined at %3%")
        % attr % pos % prevPos);
}


static void addAttr(ExprAttrs * attrs, AttrPath & attrPath,
    Expr * e, const Pos & pos)
{
    AttrPath::iterator i;
    // All attrpaths have at least one attr
    assert(!attrPath.empty());
    for (i = attrPath.begin(); i + 1 < attrPath.end(); i++) {
        if (i->symbol.set()) {
            ExprAttrs::AttrDefs::iterator j = attrs->attrs.find(i->symbol);
            if (j != attrs->attrs.end()) {
                if (!j->second.inherited) {
                    ExprAttrs * attrs2 = dynamic_cast<ExprAttrs *>(j->second.e);
                    if (!attrs2) dupAttr(attrPath, pos, j->second.pos);
                    attrs = attrs2;
                } else
                    dupAttr(attrPath, pos, j->second.pos);
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
    if (i->symbol.set()) {
        ExprAttrs::AttrDefs::iterator j = attrs->attrs.find(i->symbol);
        if (j != attrs->attrs.end()) {
            dupAttr(attrPath, pos, j->second.pos);
        } else {
            attrs->attrs[i->symbol] = ExprAttrs::AttrDef(e, pos);
            e->setName(i->symbol);
        }
    } else {
        attrs->dynamicAttrs.push_back(ExprAttrs::DynamicAttrDef(i->expr, e, pos));
    }
}


static void addFormal(const Pos & pos, Formals * formals, const Formal & formal)
{
    if (formals->argNames.find(formal.name) != formals->argNames.end())
        throw ParseError(format("duplicate formal function argument ‘%1%’ at %2%")
            % formal.name % pos);
    formals->formals.push_front(formal);
    formals->argNames.insert(formal.name);
}


static Expr * stripIndentation(const Pos & pos, SymbolTable & symbols, vector<Expr *> & es)
{
    if (es.empty()) return new ExprString(symbols.create(""));

    /* Figure out the minimum indentation.  Note that by design
       whitespace-only final lines are not taken into account.  (So
       the " " in "\n ''" is ignored, but the " " in "\n foo''" is.) */
    bool atStartOfLine = true; /* = seen only whitespace in the current line */
    unsigned int minIndent = 1000000;
    unsigned int curIndent = 0;
    for (auto & i : es) {
        ExprIndStr * e = dynamic_cast<ExprIndStr *>(i);
        if (!e) {
            /* Anti-quotations end the current start-of-line whitespace. */
            if (atStartOfLine) {
                atStartOfLine = false;
                if (curIndent < minIndent) minIndent = curIndent;
            }
            continue;
        }
        for (unsigned int j = 0; j < e->s.size(); ++j) {
            if (atStartOfLine) {
                if (e->s[j] == ' ')
                    curIndent++;
                else if (e->s[j] == '\n') {
                    /* Empty line, doesn't influence minimum
                       indentation. */
                    curIndent = 0;
                } else {
                    atStartOfLine = false;
                    if (curIndent < minIndent) minIndent = curIndent;
                }
            } else if (e->s[j] == '\n') {
                atStartOfLine = true;
                curIndent = 0;
            }
        }
    }

    /* Strip spaces from each line. */
    vector<Expr *> * es2 = new vector<Expr *>;
    atStartOfLine = true;
    unsigned int curDropped = 0;
    unsigned int n = es.size();
    for (vector<Expr *>::iterator i = es.begin(); i != es.end(); ++i, --n) {
        ExprIndStr * e = dynamic_cast<ExprIndStr *>(*i);
        if (!e) {
            atStartOfLine = false;
            curDropped = 0;
            es2->push_back(*i);
            continue;
        }

        string s2;
        for (unsigned int j = 0; j < e->s.size(); ++j) {
            if (atStartOfLine) {
                if (e->s[j] == ' ') {
                    if (curDropped++ >= minIndent)
                        s2 += e->s[j];
                }
                else if (e->s[j] == '\n') {
                    curDropped = 0;
                    s2 += e->s[j];
                } else {
                    atStartOfLine = false;
                    curDropped = 0;
                    s2 += e->s[j];
                }
            } else {
                s2 += e->s[j];
                if (e->s[j] == '\n') atStartOfLine = true;
            }
        }

        /* Remove the last line if it is empty and consists only of
           spaces. */
        if (n == 1) {
            string::size_type p = s2.find_last_of('\n');
            if (p != string::npos && s2.find_first_not_of(' ', p + 1) == string::npos)
                s2 = string(s2, 0, p + 1);
        }

        es2->push_back(new ExprString(symbols.create(s2)));
    }

    /* If this is a single string, then don't do a concatenation. */
    return es2->size() == 1 && dynamic_cast<ExprString *>((*es2)[0]) ? (*es2)[0] : new ExprConcatStrings(pos, true, es2);
}


static inline Pos makeCurPos(const YYLTYPE & loc, ParseData * data)
{
    return Pos(data->path, loc.first_line, loc.first_column);
}

#define CUR_POS makeCurPos(*yylocp, data)


}


void yyerror(YYLTYPE * loc, yyscan_t scanner, ParseData * data, const char * error)
{
    data->error = (format("%1%, at %2%")
        % error % makeCurPos(*loc, data)).str();
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
  const char * id; // !!! -> Symbol
  char * path;
  char * uri;
  std::vector<nix::AttrName> * attrNames;
  std::vector<nix::Expr *> * string_parts;
}

%type <e> start expr expr_function expr_if expr_op
%type <e> expr_app expr_select expr_simple
%type <list> expr_list
%type <attrs> binds
%type <formals> formals
%type <formal> formal
%type <attrNames> attrs attrpath
%type <string_parts> string_parts_interpolated ind_string_parts
%type <e> string_parts string_attr
%type <id> attr
%token <id> ID ATTRPATH
%token <e> STR IND_STR
%token <n> INT
%token <nf> FLOAT
%token <path> PATH HPATH SPATH
%token <uri> URI
%token IF THEN ELSE ASSERT WITH LET IN REC INHERIT EQ NEQ AND OR IMPL OR_KW
%token DOLLAR_CURLY /* == ${ */
%token IND_STRING_OPEN IND_STRING_CLOSE
%token ELLIPSIS

%nonassoc IMPL
%left OR
%left AND
%nonassoc EQ NEQ
%left '<' '>' LEQ GEQ
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
    { $$ = new ExprLambda(CUR_POS, data->symbols.create($1), false, 0, $3); }
  | '{' formals '}' ':' expr_function
    { $$ = new ExprLambda(CUR_POS, data->symbols.create(""), true, $2, $5); }
  | '{' formals '}' '@' ID ':' expr_function
    { $$ = new ExprLambda(CUR_POS, data->symbols.create($5), true, $2, $7); }
  | ID '@' '{' formals '}' ':' expr_function
    { $$ = new ExprLambda(CUR_POS, data->symbols.create($1), true, $4, $7); }
  | ASSERT expr ';' expr_function
    { $$ = new ExprAssert(CUR_POS, $2, $4); }
  | WITH expr ';' expr_function
    { $$ = new ExprWith(CUR_POS, $2, $4); }
  | LET binds IN expr_function
    { if (!$2->dynamicAttrs.empty())
        throw ParseError(format("dynamic attributes not allowed in let at %1%")
            % CUR_POS);
      $$ = new ExprLet($2, $4);
    }
  | expr_if
  ;

expr_if
  : IF expr THEN expr ELSE expr { $$ = new ExprIf($2, $4, $6); }
  | expr_op
  ;

expr_op
  : '!' expr_op %prec NOT { $$ = new ExprOpNot($2); }
  | '-' expr_op %prec NEGATE { $$ = new ExprApp(CUR_POS, new ExprApp(new ExprVar(data->symbols.create("__sub")), new ExprInt(0)), $2); }
  | expr_op EQ expr_op { $$ = new ExprOpEq($1, $3); }
  | expr_op NEQ expr_op { $$ = new ExprOpNEq($1, $3); }
  | expr_op '<' expr_op { $$ = new ExprApp(CUR_POS, new ExprApp(new ExprVar(data->symbols.create("__lessThan")), $1), $3); }
  | expr_op LEQ expr_op { $$ = new ExprOpNot(new ExprApp(CUR_POS, new ExprApp(new ExprVar(data->symbols.create("__lessThan")), $3), $1)); }
  | expr_op '>' expr_op { $$ = new ExprApp(CUR_POS, new ExprApp(new ExprVar(data->symbols.create("__lessThan")), $3), $1); }
  | expr_op GEQ expr_op { $$ = new ExprOpNot(new ExprApp(CUR_POS, new ExprApp(new ExprVar(data->symbols.create("__lessThan")), $1), $3)); }
  | expr_op AND expr_op { $$ = new ExprOpAnd(CUR_POS, $1, $3); }
  | expr_op OR expr_op { $$ = new ExprOpOr(CUR_POS, $1, $3); }
  | expr_op IMPL expr_op { $$ = new ExprOpImpl(CUR_POS, $1, $3); }
  | expr_op UPDATE expr_op { $$ = new ExprOpUpdate(CUR_POS, $1, $3); }
  | expr_op '?' attrpath { $$ = new ExprOpHasAttr($1, *$3); }
  | expr_op '+' expr_op
    { $$ = new ExprConcatStrings(CUR_POS, false, new vector<Expr *>({$1, $3})); }
  | expr_op '-' expr_op { $$ = new ExprApp(CUR_POS, new ExprApp(new ExprVar(data->symbols.create("__sub")), $1), $3); }
  | expr_op '*' expr_op { $$ = new ExprApp(CUR_POS, new ExprApp(new ExprVar(data->symbols.create("__mul")), $1), $3); }
  | expr_op '/' expr_op { $$ = new ExprApp(CUR_POS, new ExprApp(new ExprVar(data->symbols.create("__div")), $1), $3); }
  | expr_op CONCAT expr_op { $$ = new ExprOpConcatLists(CUR_POS, $1, $3); }
  | expr_app
  ;

expr_app
  : expr_app expr_select
    { $$ = new ExprApp(CUR_POS, $1, $2); }
  | expr_select { $$ = $1; }
  ;

expr_select
  : expr_simple '.' attrpath
    { $$ = new ExprSelect(CUR_POS, $1, *$3, 0); }
  | expr_simple '.' attrpath OR_KW expr_select
    { $$ = new ExprSelect(CUR_POS, $1, *$3, $5); }
  | /* Backwards compatibility: because Nixpkgs has a rarely used
       function named ‘or’, allow stuff like ‘map or [...]’. */
    expr_simple OR_KW
    { $$ = new ExprApp(CUR_POS, $1, new ExprVar(CUR_POS, data->symbols.create("or"))); }
  | expr_simple { $$ = $1; }
  ;

expr_simple
  : ID {
      if (strcmp($1, "__curPos") == 0)
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
  | PATH { $$ = new ExprPath(absPath($1, data->basePath)); }
  | HPATH { $$ = new ExprPath(getEnv("HOME", "") + string{$1 + 1}); }
  | SPATH {
      string path($1 + 1, strlen($1) - 2);
      $$ = new ExprApp(CUR_POS,
          new ExprApp(new ExprVar(data->symbols.create("__findFile")),
              new ExprVar(data->symbols.create("__nixPath"))),
          new ExprString(data->symbols.create(path)));
  }
  | URI { $$ = new ExprString(data->symbols.create($1)); }
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
  : STR
  | string_parts_interpolated { $$ = new ExprConcatStrings(CUR_POS, true, $1); }
  | { $$ = new ExprString(data->symbols.create("")); }
  ;

string_parts_interpolated
  : string_parts_interpolated STR { $$ = $1; $1->push_back($2); }
  | string_parts_interpolated DOLLAR_CURLY expr '}' { $$ = $1; $1->push_back($3); }
  | DOLLAR_CURLY expr '}' { $$ = new vector<Expr *>; $$->push_back($2); }
  | STR DOLLAR_CURLY expr '}' {
      $$ = new vector<Expr *>;
      $$->push_back($1);
      $$->push_back($3);
    }
  ;

ind_string_parts
  : ind_string_parts IND_STR { $$ = $1; $1->push_back($2); }
  | ind_string_parts DOLLAR_CURLY expr '}' { $$ = $1; $1->push_back($3); }
  | { $$ = new vector<Expr *>; }
  ;

binds
  : binds attrpath '=' expr ';' { $$ = $1; addAttr($$, *$2, $4, makeCurPos(@2, data)); }
  | binds INHERIT attrs ';'
    { $$ = $1;
      for (auto & i : *$3) {
          if ($$->attrs.find(i.symbol) != $$->attrs.end())
              dupAttr(i.symbol, makeCurPos(@3, data), $$->attrs[i.symbol].pos);
          Pos pos = makeCurPos(@3, data);
          $$->attrs[i.symbol] = ExprAttrs::AttrDef(new ExprVar(CUR_POS, i.symbol), pos, true);
      }
    }
  | binds INHERIT '(' expr ')' attrs ';'
    { $$ = $1;
      /* !!! Should ensure sharing of the expression in $4. */
      for (auto & i : *$6) {
          if ($$->attrs.find(i.symbol) != $$->attrs.end())
              dupAttr(i.symbol, makeCurPos(@6, data), $$->attrs[i.symbol].pos);
          $$->attrs[i.symbol] = ExprAttrs::AttrDef(new ExprSelect(CUR_POS, $4, i.symbol), makeCurPos(@6, data));
      }
    }
  | { $$ = new ExprAttrs; }
  ;

attrs
  : attrs attr { $$ = $1; $1->push_back(AttrName(data->symbols.create($2))); }
  | attrs string_attr
    { $$ = $1;
      ExprString * str = dynamic_cast<ExprString *>($2);
      if (str) {
          $$->push_back(AttrName(str->s));
          delete str;
      } else
          throw ParseError(format("dynamic attributes not allowed in inherit at %1%")
              % makeCurPos(@2, data));
    }
  | { $$ = new AttrPath; }
  ;

attrpath
  : attrpath '.' attr { $$ = $1; $1->push_back(AttrName(data->symbols.create($3))); }
  | attrpath '.' string_attr
    { $$ = $1;
      ExprString * str = dynamic_cast<ExprString *>($3);
      if (str) {
          $$->push_back(AttrName(str->s));
          delete str;
      } else
          $$->push_back(AttrName($3));
    }
  | attr { $$ = new vector<AttrName>; $$->push_back(AttrName(data->symbols.create($1))); }
  | string_attr
    { $$ = new vector<AttrName>;
      ExprString *str = dynamic_cast<ExprString *>($1);
      if (str) {
          $$->push_back(AttrName(str->s));
          delete str;
      } else
          $$->push_back(AttrName($1));
    }
  ;

attr
  : ID { $$ = $1; }
  | OR_KW { $$ = "or"; }
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
    { $$ = $3; addFormal(CUR_POS, $$, *$1); }
  | formal
    { $$ = new Formals; addFormal(CUR_POS, $$, *$1); $$->ellipsis = false; }
  |
    { $$ = new Formals; $$->ellipsis = false; }
  | ELLIPSIS
    { $$ = new Formals; $$->ellipsis = true; }
  ;

formal
  : ID { $$ = new Formal(data->symbols.create($1), 0); }
  | ID '?' expr { $$ = new Formal(data->symbols.create($1), $3); }
  ;

%%


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "eval.hh"
#include "download.hh"
#include "store-api.hh"
#include "primops/fetchgit.hh"


namespace nix {


Expr * EvalState::parse(const char * text,
    const Path & path, const Path & basePath, StaticEnv & staticEnv)
{
    yyscan_t scanner;
    ParseData data(*this);
    data.basePath = basePath;
    data.path = data.symbols.create(path);

    yylex_init(&scanner);
    yy_scan_string(text, scanner);
    int res = yyparse(scanner, &data);
    yylex_destroy(scanner);

    if (res) {
      if (data.atEnd)
        throw IncompleteParseError(data.error);
      else
        throw ParseError(data.error);
    }

    data.result->bindVars(staticEnv);

    return data.result;
}


Path resolveExprPath(Path path)
{
    assert(path[0] == '/');

    /* If `path' is a symlink, follow it.  This is so that relative
       path references work. */
    struct stat st;
    while (true) {
        if (lstat(path.c_str(), &st))
            throw SysError(format("getting status of ‘%1%’") % path);
        if (!S_ISLNK(st.st_mode)) break;
        path = absPath(readLink(path), dirOf(path));
    }

    /* If `path' refers to a directory, append `/default.nix'. */
    if (S_ISDIR(st.st_mode))
        path = canonPath(path + "/default.nix");

    return path;
}


Expr * EvalState::parseExprFromFile(const Path & path)
{
    return parseExprFromFile(path, staticBaseEnv);
}


Expr * EvalState::parseExprFromFile(const Path & path, StaticEnv & staticEnv)
{
    return parse(readFile(path).c_str(), path, dirOf(path), staticEnv);
}


Expr * EvalState::parseExprFromString(const string & s, const Path & basePath, StaticEnv & staticEnv)
{
    return parse(s.c_str(), "(string)", basePath, staticEnv);
}


Expr * EvalState::parseExprFromString(const string & s, const Path & basePath)
{
    return parseExprFromString(s, basePath, staticBaseEnv);
}


void EvalState::addToSearchPath(const string & s)
{
    size_t pos = s.find('=');
    string prefix;
    Path path;
    if (pos == string::npos) {
        path = s;
    } else {
        prefix = string(s, 0, pos);
        path = string(s, pos + 1);
    }

    searchPath.emplace_back(prefix, path);
}


Path EvalState::findFile(const string & path)
{
    return findFile(searchPath, path);
}


Path EvalState::findFile(SearchPath & searchPath, const string & path, const Pos & pos)
{
    for (auto & i : searchPath) {
        std::string suffix;
        if (i.first.empty())
            suffix = "/" + path;
        else {
            auto s = i.first.size();
            if (path.compare(0, s, i.first) != 0 ||
                (path.size() > s && path[s] != '/'))
                continue;
            suffix = path.size() == s ? "" : "/" + string(path, s);
        }
        auto r = resolveSearchPathElem(i);
        if (!r.first) continue;
        Path res = r.second + suffix;
        if (pathExists(res)) return canonPath(res);
    }
    format f = format(
        "file ‘%1%’ was not found in the Nix search path (add it using $NIX_PATH or -I)"
        + string(pos ? ", at %2%" : ""));
    f.exceptions(boost::io::all_error_bits ^ boost::io::too_many_args_bit);
    throw ThrownError(f % path % pos);
}


std::pair<bool, std::string> EvalState::resolveSearchPathElem(const SearchPathElem & elem)
{
    auto i = searchPathResolved.find(elem.second);
    if (i != searchPathResolved.end()) return i->second;

    std::pair<bool, std::string> res;

    if (isUri(elem.second)) {
        try {
            if (hasPrefix(elem.second, "git://") || hasSuffix(elem.second, ".git"))
                // FIXME: support specifying revision/branch
                res = { true, exportGit(store, elem.second, "master") };
            else
                res = { true, getDownloader()->downloadCached(store, elem.second, true) };
        } catch (DownloadError & e) {
            printError(format("warning: Nix search path entry ‘%1%’ cannot be downloaded, ignoring") % elem.second);
            res = { false, "" };
        }
    } else {
        auto path = absPath(elem.second);
        if (pathExists(path))
            res = { true, path };
        else {
            printError(format("warning: Nix search path entry ‘%1%’ does not exist, ignoring") % elem.second);
            res = { false, "" };
        }
    }

    debug(format("resolved search path element ‘%s’ to ‘%s’") % elem.second % res.second);

    searchPathResolved[elem.second] = res;
    return res;
}


}
