%glr-parser
%pure-parser
%locations
%error-verbose
%defines
/* %no-lines */
%parse-param { yyscan_t scanner }
%parse-param { ParseData * data }
%lex-param { yyscan_t scanner }


%{
/* Newer versions of Bison copy the declarations below to
   parser-tab.hh, which sucks bigtime since lexer.l doesn't want that
   stuff.  So allow it to be excluded. */
#ifndef BISON_HEADER_HACK
#define BISON_HEADER_HACK
    
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aterm.hh"
#include "util.hh"
    
#include "parser-tab.hh"
#include "lexer-tab.hh"

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


static Expr fixAttrs(int recursive, ATermList as)
{
    ATermList bs = ATempty, cs = ATempty;
    ATermList * is = recursive ? &cs : &bs;
    for (ATermIterator i(as); i; ++i) {
        ATermList names;
        Expr src;
        ATerm pos;
        if (matchInherit(*i, src, names, pos)) {
            bool fromScope = matchScope(src);
            for (ATermIterator j(names); j; ++j) {
                Expr rhs = fromScope ? makeVar(*j) : makeSelect(src, *j);
                *is = ATinsert(*is, makeBind(*j, rhs, pos));
            }
        } else bs = ATinsert(bs, *i);
    }
    if (recursive)
        return makeRec(bs, cs);
    else
        return makeAttrs(bs);
}


void backToString(yyscan_t scanner);

 
static Pos makeCurPos(YYLTYPE * loc, ParseData * data)
{
    return makePos(toATerm(data->path),
        loc->first_line, loc->first_column);
}

#define CUR_POS makeCurPos(yylocp, data)


}


void yyerror(YYLTYPE * loc, yyscan_t scanner, ParseData * data, const char * error)
{
    data->error = (format("%1%, at `%2%':%3%:%4%")
        % error % data->path % loc->first_line % loc->first_column).str();
}


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


#endif


%}

%union {
  ATerm t;
  ATermList ts;
}

%type <t> start expr expr_function expr_if expr_op
%type <t> expr_app expr_select expr_simple bind inheritsrc formal
%type <ts> binds ids expr_list formals string_parts
%token <t> ID INT STR PATH URI
%token IF THEN ELSE ASSERT WITH LET IN REC INHERIT EQ NEQ AND OR IMPL
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

start: expr { data->result = $1; };

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
  | LET binds IN expr_function
    { $$ = makeSelect(fixAttrs(1, ATinsert($2, makeBind(toATerm("<let-body>"), $4, CUR_POS))), toATerm("<let-body>")); }
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
      if ($2 == ATempty) $$ = makeStr(toATerm(""), ATempty);
      else if (ATgetNext($2) == ATempty) $$ = ATgetFirst($2);
      else $$ = makeConcatStrings(ATreverse($2));
  }
  | PATH { $$ = makePath(toATerm(absPath(aterm2String($1), data->basePath))); }
  | URI { $$ = makeStr($1, ATempty); }
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


#include "eval.hh"  

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


namespace nix {
      

static void checkAttrs(ATermMap & names, ATermList bnds)
{
    for (ATermIterator i(bnds); i; ++i) {
        ATerm name;
        Expr e;
        ATerm pos;
        if (!matchBind(*i, name, e, pos)) abort(); /* can't happen */
        if (names.get(name))
            throw EvalError(format("duplicate attribute `%1%' at %2%")
                % aterm2String(name) % showPos(pos));
        names.set(name, name);
    }
}


static void checkAttrSets(ATerm e)
{
    ATermList formals;
    ATerm body, pos;
    if (matchFunction(e, formals, body, pos)) {
        ATermMap names(ATgetLength(formals));
        for (ATermIterator i(formals); i; ++i) {
            ATerm name;
            ATerm d1, d2;
            if (!matchFormal(*i, name, d1, d2)) abort();
            if (names.get(name))
                throw EvalError(format("duplicate formal function argument `%1%' at %2%")
                    % aterm2String(name) % showPos(pos));
            names.set(name, name);
        }
    }

    ATermList bnds;
    if (matchAttrs(e, bnds)) {
        ATermMap names(ATgetLength(bnds));
        checkAttrs(names, bnds);
    }
    
    ATermList rbnds, nrbnds;
    if (matchRec(e, rbnds, nrbnds)) {
        ATermMap names(ATgetLength(rbnds) + ATgetLength(nrbnds));
        checkAttrs(names, rbnds);
        checkAttrs(names, nrbnds);
    }
    
    if (ATgetType(e) == AT_APPL) {
        int arity = ATgetArity(ATgetAFun(e));
        for (int i = 0; i < arity; ++i)
            checkAttrSets(ATgetArgument(e, i));
    }

    else if (ATgetType(e) == AT_LIST)
        for (ATermIterator i((ATermList) e); i; ++i)
            checkAttrSets(*i);
}


static Expr parse(EvalState & state,
    const char * text, const Path & path,
    const Path & basePath)
{
    yyscan_t scanner;
    ParseData data;
    data.basePath = basePath;
    data.path = path;

    yylex_init(&scanner);
    yy_scan_string(text, scanner);
    int res = yyparse(scanner, &data);
    yylex_destroy(scanner);
    
    if (res) throw EvalError(data.error);

    try {
        checkVarDefs(state.primOps, data.result);
    } catch (Error & e) {
        throw EvalError(format("%1%, in `%2%'") % e.msg() % path);
    }
    
    checkAttrSets(data.result);

    return data.result;
}


Expr parseExprFromFile(EvalState & state, Path path)
{
    SwitchToOriginalUser sw;

    assert(path[0] == '/');

#if 0
    /* Perhaps this is already an imploded parse tree? */
    Expr e = ATreadFromNamedFile(path.c_str());
    if (e) return e;
#endif

    /* If `path' is a symlink, follow it.  This is so that relative
       path references work. */
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting status of `%1%'") % path);
    if (S_ISLNK(st.st_mode)) path = absPath(readLink(path), dirOf(path));

    /* If `path' refers to a directory, append `/default.nix'. */
    if (stat(path.c_str(), &st))
        throw SysError(format("getting status of `%1%'") % path);
    if (S_ISDIR(st.st_mode))
        path = canonPath(path + "/default.nix");

    /* Read the input file.  We can't use SGparseFile() because it's
       broken, so we read the input ourselves and call
       SGparseString(). */
    AutoCloseFD fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) throw SysError(format("opening `%1%'") % path);

    if (fstat(fd, &st) == -1)
        throw SysError(format("statting `%1%'") % path);

    char text[st.st_size + 1];
    readFull(fd, (unsigned char *) text, st.st_size);
    text[st.st_size] = 0;

    return parse(state, text, path, dirOf(path));
}


Expr parseExprFromString(EvalState & state,
    const string & s, const Path & basePath)
{
    return parse(state, s.c_str(), "(string)", basePath);
}

 
}
