%glr-parser
%pure-parser
%locations
%error-verbose
%parse-param { yyscan_t scanner }
%parse-param { ParseData * data }
%lex-param { yyscan_t scanner }

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <aterm2.h>

#include "parser-tab.hh"
extern "C" {
#include "lexer-tab.h"
}

#include "aterm.hh"
    
#include "nixexpr.hh"
#include "nixexpr-ast.hh"

using namespace nix;

namespace nix {
 
struct ParseData 
{
    Expr result;
    Path basePath;
    Path path;
    string error;
};

void setParseResult(ParseData * data, ATerm t);
void parseError(ParseData * data, char * error, int line, int column);
ATerm absParsedPath(ParseData * data, ATerm t);
ATerm fixAttrs(int recursive, ATermList as);
const char * getPath(ParseData * data);
Expr unescapeStr(const char * s);
 
extern "C" {
    void backToString(yyscan_t scanner);
}
 
}

void yyerror(YYLTYPE * loc, yyscan_t scanner, ParseData * data, char * s)
{
    parseError(data, s, loc->first_line, loc->first_column);
}

static Pos makeCurPos(YYLTYPE * loc, ParseData * data)
{
    return makePos(toATerm(getPath(data)),
        loc->first_line, loc->first_column);
}

#define CUR_POS makeCurPos(yylocp, data)


/* Make sure that the parse stack is scanned by the ATerm garbage
   collector. */
static void * mallocAndProtect(size_t size)
{
    void * p = malloc(size);
    if (p) ATprotectMemory(p, size);
    return p;
}

static void freeAndUnprotect(void * p)
{
    ATunprotectMemory(p);
    free(p);
}

#define YYMALLOC mallocAndProtect
#define YYFREE freeAndUnprotect

 
%}

%union {
  ATerm t;
  ATermList ts;
}

%type <t> start expr expr_function expr_if expr_op
%type <t> expr_app expr_select expr_simple bind inheritsrc formal
%type <ts> binds ids expr_list formals string_parts
%token <t> ID INT STR PATH URI
%token IF THEN ELSE ASSERT WITH LET REC INHERIT EQ NEQ AND OR IMPL
%token DOLLAR_CURLY /* == ${ */

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

start: expr { setParseResult(data, $1); };

expr: expr_function;

expr_function
  : '{' formals '}' ':' expr_function
    { $$ = makeFunction($2, $5, CUR_POS); }
  | ID ':' expr_function
    { $$ = makeFunction1($1, $3, CUR_POS); }
  | ASSERT expr ';' expr_function
    { $$ = makeAssert($2, $4, CUR_POS); }
  | WITH expr ';' expr_function
    { $$ = makeWith($2, $4, CUR_POS); }
  | expr_if
  ;

expr_if
  : IF expr THEN expr ELSE expr
    { $$ = makeIf($2, $4, $6); }
  | expr_op
  ;

expr_op
  : '!' expr_op %prec NEG { $$ = makeOpNot($2); }
  | expr_op EQ expr_op { $$ = makeOpEq($1, $3); }
  | expr_op NEQ expr_op { $$ = makeOpNEq($1, $3); }
  | expr_op AND expr_op { $$ = makeOpAnd($1, $3); }
  | expr_op OR expr_op { $$ = makeOpOr($1, $3); }
  | expr_op IMPL expr_op { $$ = makeOpImpl($1, $3); }
  | expr_op UPDATE expr_op { $$ = makeOpUpdate($1, $3); }
  | expr_op '~' expr_op { $$ = makeSubPath($1, $3); }
  | expr_op '?' ID { $$ = makeOpHasAttr($1, $3); }
  | expr_op '+' expr_op { $$ = makeOpPlus($1, $3); }
  | expr_op CONCAT expr_op { $$ = makeOpConcat($1, $3); }
  | expr_app
  ;

expr_app
  : expr_app expr_select
    { $$ = makeCall($1, $2); }
  | expr_select { $$ = $1; }
  ;

expr_select
  : expr_select '.' ID
    { $$ = makeSelect($1, $3); }
  | expr_simple { $$ = $1; }
  ;

expr_simple
  : ID { $$ = makeVar($1); }
  | INT { $$ = makeInt(ATgetInt((ATermInt) $1)); }
  | '"' string_parts '"' {
      /* For efficiency, and to simplify parse trees a bit. */
      if ($2 == ATempty) $$ = makeStr(toATerm(""));
      else if (ATgetNext($2) == ATempty) $$ = ATgetFirst($2);
      else $$ = makeConcatStrings(ATreverse($2));
  }
  | PATH { $$ = makePath(absParsedPath(data, $1)); }
  | URI { $$ = makeUri($1); }
  | '(' expr ')' { $$ = $2; }
  /* Let expressions `let {..., body = ...}' are just desugared
     into `(rec {..., body = ...}).body'. */
  | LET '{' binds '}'
    { $$ = makeSelect(fixAttrs(1, $3), toATerm("body")); }
  | REC '{' binds '}'
    { $$ = fixAttrs(1, $3); }
  | '{' binds '}'
    { $$ = fixAttrs(0, $2); }
  | '[' expr_list ']' { $$ = makeList($2); }
  ;

string_parts
  : string_parts STR { $$ = ATinsert($1, $2); }
  | string_parts DOLLAR_CURLY expr '}' { backToString(scanner); $$ = ATinsert($1, $3); }
  | { $$ = ATempty; }
  ;

binds
  : binds bind { $$ = ATinsert($1, $2); }
  | { $$ = ATempty; }
  ;

bind
  : ID '=' expr ';'
    { $$ = makeBind($1, $3, CUR_POS); }
  | INHERIT inheritsrc ids ';'
    { $$ = makeInherit($2, $3, CUR_POS); }
  ;

inheritsrc
  : '(' expr ')' { $$ = $2; }
  | { $$ = makeScope(); }
  ;

ids: ids ID { $$ = ATinsert($1, $2); } | { $$ = ATempty; };

expr_list
  : expr_select expr_list { $$ = ATinsert($2, $1); }
    /* yes, this is right-recursive, but it doesn't matter since
       otherwise we would need ATreverse which requires unbounded
       stack space */
  | { $$ = ATempty; }
  ;

formals
  : formal ',' formals { $$ = ATinsert($3, $1); } /* idem - right recursive */
  | formal { $$ = ATinsert(ATempty, $1); }
  ;

formal
  : ID { $$ = makeFormal($1, makeUnrestrictedValues(), makeNoDefaultValue()); }
  | ID ':' '[' expr_list ']' { $$ = makeFormal($1, makeValidValues($4), makeNoDefaultValue()); }
  | ID '?' expr { $$ = makeFormal($1, makeUnrestrictedValues(), makeDefaultValue($3)); }
  ;
  
%%
