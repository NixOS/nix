#ifndef __PRIMOPS_H
#define __PRIMOPS_H

#include "eval.hh"


/* Load and evaluate an expression from path specified by the
   argument. */ 
Expr primImport(EvalState & state, Expr arg);


/* Construct (as a unobservable) side effect) a Nix derivation
   expression that performs the derivation described by the argument
   set.  Returns the original set extended with the following
   attributes: `outPath' containing the primary output path of the
   derivation; `drvPath' containing the path of the Nix expression;
   and `type' set to `derivation' to indicate that this is a
   derivation. */
Expr primDerivation(EvalState & state, Expr args);


#endif /* !__PRIMOPS_H */
