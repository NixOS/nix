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

/* Return the base name of the given string, i.e., everything
   following the last slash. */
Expr primBaseNameOf(EvalState & state, Expr arg);

/* Convert the argument (which can be a path or a uri) to a string. */
Expr primToString(EvalState & state, Expr arg);

/* Return the null value. */
Expr primNull(EvalState & state);

/* Determine whether the argument is the null value. */
Expr primIsNull(EvalState & state, Expr arg);


#endif /* !__PRIMOPS_H */
