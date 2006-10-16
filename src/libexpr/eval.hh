#ifndef __EVAL_H
#define __EVAL_H

#include <map>

#include "aterm.hh"
#include "nixexpr.hh"


namespace nix {


class Hash;
    

typedef std::map<Path, PathSet> DrvRoots;
typedef std::map<Path, Hash> DrvHashes;

/* Cache for calls to addToStore(); maps source paths to the store
   paths. */
typedef std::map<Path, Path> SrcToStore;

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
string evalString(EvalState & state, Expr e, PathSet & context);
string evalStringNoCtx(EvalState & state, Expr e);
int evalInt(EvalState & state, Expr e);
bool evalBool(EvalState & state, Expr e);
ATermList evalList(EvalState & state, Expr e);

/* Flatten nested lists into a single list (or expand a singleton into
   a list). */
ATermList flattenList(EvalState & state, Expr e);

/* String coercion.  Converts strings, paths and derivations to a
   string.  If `coerceMore' is set, also converts nulls, integers,
   booleans and lists to a string. */
string coerceToString(EvalState & state, Expr e, PathSet & context,
    bool coerceMore = false, bool copyToStore = true);

/* Path coercion.  Converts strings, paths and derivations to a path.
   The result is guaranteed to be an canonicalised, absolute path.
   Nothing is copied to the store. */
Path coerceToPath(EvalState & state, Expr e, PathSet & context);

/* Automatically call a function for which each argument has a default
   value or has a binding in the `args' map.  Note: result is a call,
   not a normal form; it should be evaluated by calling evalExpr(). */
Expr autoCallFunction(Expr e, const ATermMap & args);

/* Print statistics. */
void printEvalStats(EvalState & state);

 
}


#endif /* !__EVAL_H */
