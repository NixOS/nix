#ifndef __EVAL_H
#define __EVAL_H

#include <map>

#include "aterm.hh"
#include "hash.hh"
#include "nixexpr.hh"


typedef map<Path, PathSet> DrvPaths;
typedef map<Path, Hash> DrvHashes;

struct EvalState;
typedef Expr (* PrimOp0) (EvalState &);
typedef Expr (* PrimOp1) (EvalState &, Expr arg);


struct EvalState 
{
    ATermMap normalForms;
    ATermMap primOps0; /* nullary primops */
    ATermMap primOps1; /* unary primops */
    ATermMap primOpsAll;
    DrvPaths drvPaths;
    DrvHashes drvHashes; /* normalised derivation hashes */
    Expr blackHole;

    unsigned int nrEvaluated;
    unsigned int nrCached;

    EvalState();

    void addPrimOp0(const string & name, PrimOp0 primOp);
    void addPrimOp1(const string & name, PrimOp1 primOp);
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
