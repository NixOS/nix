%glr-parser
%pure-parser
%locations
%error-verbose
%parse-param { yyscan_t scanner }
%parse-param { void * data }
%lex-param { yyscan_t scanner }

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <aterm2.h>

#include "parser-tab.h"
#include "lexer-tab.h"

void setParseResult(void * data, ATerm t);
void parseError(void * data, char * error, int line, int column);
ATerm absParsedPath(void * data, ATerm t);
ATerm fixAttrs(int recursive, ATermList as);

void yyerror(YYLTYPE * loc, yyscan_t scanner, void * data, char * s)
{
    parseError(data, s, loc->first_line, loc->first_column);
}
 
%}

%union {
  ATerm t;
  ATermList ts;
}

%type <t> start expr expr_function expr_assert expr_if expr_op
%type <t> expr_app expr_select expr_simple bind inheritsrc formal
%type <ts> binds ids expr_list formals
%token <t> ID INT STR PATH URI
%token IF THEN ELSE ASSERT LET REC INHERIT EQ NEQ AND OR IMPL

%nonassoc IMPL
%left OR
%left AND
%nonassoc EQ NEQ
%right UPDATE
%left NEG

%%

start: expr { setParseResult(data, $1); };

expr: expr_function;

expr_function
  : '{' formals '}' ':' expr_function
    { $$ = ATmake("Function(<term>, <term>)", $2, $5); }
  | ID ':' expr_function
    { $$ = ATmake("Function1(<term>, <term>)", $1, $3); }
  | expr_assert
  ;

expr_assert
  : ASSERT expr ';' expr_assert
    { $$ = ATmake("Assert(<term>, <term>)", $2, $4); }
  | expr_if
  ;

expr_if
  : IF expr THEN expr ELSE expr
    { $$ = ATmake("If(<term>, <term>, <term>)", $2, $4, $6); }
  | expr_op
  ;

expr_op
  : '!' expr_op %prec NEG { $$ = ATmake("OpNot(<term>)", $2); }
  | expr_op EQ expr_op { $$ = ATmake("OpEq(<term>, <term>)", $1, $3); }
  | expr_op NEQ expr_op { $$ = ATmake("OpNEq(<term>, <term>)", $1, $3); }
  | expr_op AND expr_op { $$ = ATmake("OpAnd(<term>, <term>)", $1, $3); }
  | expr_op OR expr_op { $$ = ATmake("OpOr(<term>, <term>)", $1, $3); }
  | expr_op IMPL expr_op { $$ = ATmake("OpImpl(<term>, <term>)", $1, $3); }
  | expr_op UPDATE expr_op { $$ = ATmake("OpUpdate(<term>, <term>)", $1, $3); }
  | expr_app
  ;

expr_app
  : expr_app expr_select
    { $$ = ATmake("Call(<term>, <term>)", $1, $2); }
  | expr_select { $$ = $1; }
  ;

expr_select
  : expr_select '.' ID
    { $$ = ATmake("Select(<term>, <term>)", $1, $3); }
  | expr_simple { $$ = $1; }
  ;

expr_simple
  : ID { $$ = ATmake("Var(<term>)", $1); }
  | INT { $$ = ATmake("Int(<term>)", $1); }
  | STR { $$ = ATmake("Str(<term>)", $1); }
  | PATH { $$ = ATmake("Path(<term>)", absParsedPath(data, $1)); }
  | URI { $$ = ATmake("Uri(<term>)", $1); }
  | '(' expr ')' { $$ = $2; }
  /* Let expressions `let {..., body = ...}' are just desugared
     into `(rec {..., body = ...}).body'. */
  | LET '{' binds '}'
    { $$ = ATmake("Select(<term>, \"body\")", fixAttrs(1, $3)); }
  | REC '{' binds '}'
    { $$ = fixAttrs(1, $3); }
  | '{' binds '}'
    { $$ = fixAttrs(0, $2); }
  | '[' expr_list ']' { $$ = ATmake("List(<term>)", $2); }
  ;

binds
  : binds bind { $$ = ATinsert($1, $2); }
  | { $$ = ATempty; }
  ;

bind
  : ID '=' expr ';'
    { $$ = ATmake("Bind(<term>, <term>)", $1, $3); }
  | INHERIT inheritsrc ids ';'
    { $$ = ATmake("Inherit(<term>, <term>)", $2, $3); }
  ;

inheritsrc
  : '(' expr ')' { $$ = $2; }
  | { $$ = ATmake("Scope"); }
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
  : ID { $$ = ATmake("NoDefFormal(<term>)", $1); }
  | ID '?' expr { $$ = ATmake("DefFormal(<term>, <term>)", $1, $3); }
  ;
  
%%
