#ifndef __EVAL_H
#define __EVAL_H

#include <map>

#include "fix-expr.hh"


typedef map<Expr, Expr> NormalForms;
//typedef map<Path, PathSet> PkgPaths;
//typedef map<Path, Hash> PkgHashes;

struct EvalState 
{
    NormalForms normalForms;
    //    PkgPaths pkgPaths;
    //    PkgHashes pkgHashes; /* normalised package hashes */
    Expr blackHole;

    EvalState();
};


/* Evaluate an expression to normal form. */
Expr evalExpr(EvalState & state, Expr e);

/* Evaluate an expression read from the given file to normal form. */
Expr evalFile(EvalState & state, const Path & path);


#endif /* !__EVAL_H */
