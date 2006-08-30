#ifndef __EVAL_H
#define __EVAL_H

#include <map>

#include "aterm.hh"
#include "hash.hh"
#include "nixexpr.hh"


typedef map<Path, PathSet> DrvRoots;
typedef map<Path, Hash> DrvHashes;

/* Cache for calls to addToStore(); maps source paths to the store
   paths. */
typedef map<Path, Path> SrcToStore;

struct EvalState;

/* Note: using a ATermVector is safe here, since when we call a primop
   we also have an ATermList on the stack. */
typedef Expr (* PrimOp) (EvalState &, const ATermVector & args);


struct EvalState 
{
    ATermMap normalForms;
    ATermMap primOps;
    DrvRoots drvRoots;
    DrvHashes drvHashes; /* normalised derivation hashes */
    SrcToStore srcToStore; 

    unsigned int nrEvaluated;
    unsigned int nrCached;

    EvalState();

    void addPrimOps();
    void addPrimOp(const string & name,
        unsigned int arity, PrimOp primOp);
};


/* Evaluate an expression to normal form. */
Expr evalExpr(EvalState & state, Expr e);

/* Evaluate an expression read from the given file to normal form. */
Expr evalFile(EvalState & state, const Path & path);

/* Evaluate an expression, and recursively evaluate list elements and
   attributes.  If `canonicalise' is true, we remove things like
   position information and make sure that attribute sets are in
   sorded order. */
Expr strictEvalExpr(EvalState & state, Expr e,
    bool canonicalise = false);

/* Specific results. */
string evalString(EvalState & state, Expr e);
Path evalPath(EvalState & state, Expr e);
bool evalBool(EvalState & state, Expr e);
ATermList evalList(EvalState & state, Expr e);
ATerm coerceToString(Expr e);

/* Contexts. */
string coerceToStringWithContext(EvalState & state,
    ATermList & context, Expr e, bool & isPath);
Expr wrapInContext(ATermList context, Expr e);

/* Automatically call a function for which each argument has a default
   value or has a binding in the `args' map.  Note: result is a call,
   not a normal form; it should be evaluated by calling evalExpr(). */
Expr autoCallFunction(Expr e, const ATermMap & args);

/* Print statistics. */
void printEvalStats(EvalState & state);


#endif /* !__EVAL_H */
