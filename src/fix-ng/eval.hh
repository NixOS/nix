#ifndef __EVAL_H
#define __EVAL_H

#include <map>

#include "fix-expr.hh"
#include "expr.hh"


typedef map<Expr, Expr> NormalForms;
typedef map<Path, PathSet> DrvPaths;
typedef map<Path, Hash> DrvHashes;

struct EvalState 
{
    NormalForms normalForms;
    DrvPaths drvPaths;
    DrvHashes drvHashes; /* normalised derivation hashes */
    Expr blackHole;

    unsigned int nrEvaluated;
    unsigned int nrCached;

    EvalState();
};


/* Evaluate an expression to normal form. */
Expr evalExpr(EvalState & state, Expr e);

/* Evaluate an expression read from the given file to normal form. */
Expr evalFile(EvalState & state, const Path & path);

/* Specific results. */
string evalString(EvalState & state, Expr e);
Path evalPath(EvalState & state, Expr e);

/* Print statistics. */
void printEvalStats(EvalState & state);


#endif /* !__EVAL_H */
