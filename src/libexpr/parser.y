%glr-parser
%pure-parser
%locations
%error-verbose
%defines
/* %no-lines */
%parse-param { yyscan_t scanner }
%parse-param { ParseData * data }
%lex-param { yyscan_t scanner }
%lex-param { ParseData * data }

%code requires {
    
#ifndef BISON_HEADER
#define BISON_HEADER
    
#include "util.hh"
    
#include "nixexpr.hh"

namespace nix {

    struct ParseData 
    {
        SymbolTable & symbols;
        Expr * result;
        Path basePath;
        Path path;
        string error;
        Symbol sLetBody;
        ParseData(SymbolTable & symbols)
            : symbols(symbols)
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
#define YYSTYPE YYSTYPE // workaround a bug in Bison 2.4

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

YY_DECL;

using namespace nix;


namespace nix {
    

static string showAttrPath(const vector<Symbol> & attrPath)
{
    string s;
    foreach (vector<Symbol>::const_iterator, i, attrPath) {
        if (!s.empty()) s += '.';
        s += *i;
    }
    return s;
}


static void dupAttr(const vector<Symbol> & attrPath, const Pos & pos, const Pos & prevPos)
{
    throw ParseError(format("attribute `%1%' at %2% already defined at %3%")
        % showAttrPath(attrPath) % pos % prevPos);
}
 

static void dupAttr(Symbol attr, const Pos & pos, const Pos & prevPos)
{
    vector<Symbol> attrPath; attrPath.push_back(attr);
    throw ParseError(format("attribute `%1%' at %2% already defined at %3%")
        % showAttrPath(attrPath) % pos % prevPos);
}
 

static void addAttr(ExprAttrs * attrs, const vector<Symbol> & attrPath,
    Expr * e, const Pos & pos)
{
    unsigned int n = 0;
    foreach (vector<Symbol>::const_iterator, i, attrPath) {
        n++;
        ExprAttrs::AttrDefs::iterator j = attrs->attrs.find(*i);
        if (j != attrs->attrs.end()) {
            if (!j->second.inherited) {
                ExprAttrs * attrs2 = dynamic_cast<ExprAttrs *>(j->second.e);
                if (!attrs2 || n == attrPath.size()) dupAttr(attrPath, pos, j->second.pos);
                attrs = attrs2;
            } else
                dupAttr(attrPath, pos, j->second.pos);
        } else {
            if (n == attrPath.size())
                attrs->attrs[*i] = ExprAttrs::AttrDef(e, pos);
            else {
                ExprAttrs * nested = new ExprAttrs;
                attrs->attrs[*i] = ExprAttrs::AttrDef(nested, pos);
                attrs = nested;
            }
        }
    }
}


static void addFormal(const Pos & pos, Formals * formals, const Formal & formal)
{
    if (formals->argNames.find(formal.name) != formals->argNames.end())
        throw ParseError(format("duplicate formal function argument `%1%' at %2%")
            % formal.name % pos);
    formals->formals.push_front(formal);
    formals->argNames.insert(formal.name);
}


static Expr * stripIndentation(SymbolTable & symbols, vector<Expr *> & es)
{
    if (es.empty()) return new ExprString(symbols.create(""));
    
    /* Figure out the minimum indentation.  Note that by design
       whitespace-only final lines are not taken into account.  (So
       the " " in "\n ''" is ignored, but the " " in "\n foo''" is.) */
    bool atStartOfLine = true; /* = seen only whitespace in the current line */
    unsigned int minIndent = 1000000;
    unsigned int curIndent = 0;
    foreach (vector<Expr *>::iterator, i, es) {
        ExprIndStr * e = dynamic_cast<ExprIndStr *>(*i);
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

    return new ExprConcatStrings(es2);
}


void backToString(yyscan_t scanner);
void backToIndString(yyscan_t scanner);


static Pos makeCurPos(const YYLTYPE & loc, ParseData * data)
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
  nix::Expr * e;
  nix::ExprList * list;
  nix::ExprAttrs * attrs;
  nix::Formals * formals;
  nix::Formal * formal;
  int n;
  char * id; // !!! -> Symbol
  char * path;
  char * uri;
  std::vector<nix::Symbol> * ids;
  std::vector<nix::Expr *> * string_parts;
}

%type <e> start expr expr_function expr_if expr_op
%type <e> expr_app expr_select expr_simple
%type <list> expr_list
%type <attrs> binds
%type <formals> formals
%type <formal> formal
%type <ids> ids attrpath
%type <string_parts> string_parts ind_string_parts
%token <id> ID ATTRPATH
%token <e> STR IND_STR
%token <n> INT
%token <path> PATH
%token <uri> URI
%token IF THEN ELSE ASSERT WITH LET IN REC INHERIT EQ NEQ AND OR IMPL
%token DOLLAR_CURLY /* == ${ */
%token IND_STRING_OPEN IND_STRING_CLOSE
%token ELLIPSIS

%nonassoc IMPL
%left OR
%left AND
%nonassoc EQ NEQ
%right UPDATE
%left NEG
%left '+'
%right CONCAT
%nonassoc '?'
%nonassoc '~'

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
    { $$ = new ExprLet($2, $4); }
  | expr_if
  ;

expr_if
  : IF expr THEN expr ELSE expr { $$ = new ExprIf($2, $4, $6); }
  | expr_op
  ;

expr_op
  : '!' expr_op %prec NEG { $$ = new ExprOpNot($2); }
  | expr_op EQ expr_op { $$ = new ExprOpEq($1, $3); }
  | expr_op NEQ expr_op { $$ = new ExprOpNEq($1, $3); }
  | expr_op AND expr_op { $$ = new ExprOpAnd($1, $3); }
  | expr_op OR expr_op { $$ = new ExprOpOr($1, $3); }
  | expr_op IMPL expr_op { $$ = new ExprOpImpl($1, $3); }
  | expr_op UPDATE expr_op { $$ = new ExprOpUpdate($1, $3); }
  | expr_op '?' ID { $$ = new ExprOpHasAttr($1, data->symbols.create($3)); }
  | expr_op '+' expr_op
    { vector<Expr *> * l = new vector<Expr *>;
      l->push_back($1);
      l->push_back($3);
      $$ = new ExprConcatStrings(l);
    }
  | expr_op CONCAT expr_op { $$ = new ExprOpConcatLists($1, $3); }
  | expr_app
  ;

expr_app
  : expr_app expr_select
    { $$ = new ExprApp($1, $2); }
  | expr_select { $$ = $1; }
  ;

expr_select
  : expr_select '.' ID
    { $$ = new ExprSelect($1, data->symbols.create($3)); }
  | expr_simple { $$ = $1; }
  ;

expr_simple
  : ID { $$ = new ExprVar(data->symbols.create($1)); }
  | INT { $$ = new ExprInt($1); }
  | '"' string_parts '"' {
      /* For efficiency, and to simplify parse trees a bit. */
      if ($2->empty()) $$ = new ExprString(data->symbols.create(""));
      else if ($2->size() == 1) $$ = $2->front();
      else $$ = new ExprConcatStrings($2);
  }
  | IND_STRING_OPEN ind_string_parts IND_STRING_CLOSE {
      $$ = stripIndentation(data->symbols, *$2);
  }
  | PATH { $$ = new ExprPath(absPath($1, data->basePath)); }
  | URI { $$ = new ExprString(data->symbols.create($1)); }
  | '(' expr ')' { $$ = $2; }
  /* Let expressions `let {..., body = ...}' are just desugared
     into `(rec {..., body = ...}).body'. */
  | LET '{' binds '}'
    { $3->recursive = true; $$ = new ExprSelect($3, data->symbols.create("body")); }
  | REC '{' binds '}'
    { $3->recursive = true; $$ = $3; }
  | '{' binds '}'
    { $$ = $2; }
  | '[' expr_list ']' { $$ = $2; }
  ;

string_parts
  : string_parts STR { $$ = $1; $1->push_back($2); }
  | string_parts DOLLAR_CURLY expr '}' { backToString(scanner); $$ = $1; $1->push_back($3); }
  | { $$ = new vector<Expr *>; }
  ;

ind_string_parts
  : ind_string_parts IND_STR { $$ = $1; $1->push_back($2); }
  | ind_string_parts DOLLAR_CURLY expr '}' { backToIndString(scanner); $$ = $1; $1->push_back($3); }
  | { $$ = new vector<Expr *>; }
  ;

binds
  : binds attrpath '=' expr ';' { $$ = $1; addAttr($$, *$2, $4, makeCurPos(@2, data)); }
  | binds INHERIT ids ';'
    { $$ = $1;
      foreach (vector<Symbol>::iterator, i, *$3) {
          if ($$->attrs.find(*i) != $$->attrs.end())
              dupAttr(*i, makeCurPos(@3, data), $$->attrs[*i].pos);
          Pos pos = makeCurPos(@3, data);
          $$->attrs[*i] = ExprAttrs::AttrDef(*i, pos);
      }
    }
  | binds INHERIT '(' expr ')' ids ';'
    { $$ = $1;
      /* !!! Should ensure sharing of the expression in $4. */
      foreach (vector<Symbol>::iterator, i, *$6) {
          if ($$->attrs.find(*i) != $$->attrs.end())
              dupAttr(*i, makeCurPos(@6, data), $$->attrs[*i].pos);
          $$->attrs[*i] = ExprAttrs::AttrDef(new ExprSelect($4, *i), makeCurPos(@6, data));
      }}

  | { $$ = new ExprAttrs; }
  ;

ids
  : ids ID { $$ = $1; $1->push_back(data->symbols.create($2)); /* !!! dangerous */ }
  | { $$ = new vector<Symbol>; }
  ;

attrpath
  : attrpath '.' ID { $$ = $1; $1->push_back(data->symbols.create($3)); }
  | ID { $$ = new vector<Symbol>; $$->push_back(data->symbols.create($1)); }
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

#include <eval.hh>


namespace nix {
      

static Expr * parse(EvalState & state, const char * text,
    const Path & path, const Path & basePath)
{
    yyscan_t scanner;
    ParseData data(state.symbols);
    data.basePath = basePath;
    data.path = path;

    yylex_init(&scanner);
    yy_scan_string(text, scanner);
    int res = yyparse(scanner, &data);
    yylex_destroy(scanner);
    
    if (res) throw ParseError(data.error);

    try {
        data.result->bindVars(state.staticBaseEnv);
    } catch (Error & e) {
        throw ParseError(format("%1%, in `%2%'") % e.msg() % path);
    }
    
    return data.result;
}


Expr * parseExprFromFile(EvalState & state, Path path)
{
    assert(path[0] == '/');

    /* If `path' is a symlink, follow it.  This is so that relative
       path references work. */
    struct stat st;
    while (true) {
        if (lstat(path.c_str(), &st))
            throw SysError(format("getting status of `%1%'") % path);
        if (!S_ISLNK(st.st_mode)) break;
        path = absPath(readLink(path), dirOf(path));
    }

    /* If `path' refers to a directory, append `/default.nix'. */
    if (S_ISDIR(st.st_mode))
        path = canonPath(path + "/default.nix");

    /* Read and parse the input file. */
    return parse(state, readFile(path).c_str(), path, dirOf(path));
}


Expr * parseExprFromString(EvalState & state,
    const string & s, const Path & basePath)
{
    return parse(state, s.c_str(), "(string)", basePath);
}

 
}
