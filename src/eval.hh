#ifndef __EVAL_H
#define __EVAL_H

extern "C" {
#include <aterm2.h>
}

#include "hash.hh"

using namespace std;


/* Abstract syntax of Nix expressions.

   An expression describes a (partial) state of the file system in a
   referentially transparent way.  The operational effect of
   evaluating an expression is that the state described by the
   expression is realised.

     File : String * Content * [Expr] -> Expr

   File(path, content, refs) specifies a file object (its full path
   and contents), along with all file objects referenced by it (that
   is, that it has pointers to).  We assume that all files are
   self-referential.  This prevents us from having to deal with
   cycles.

     Derive : String * Expr * [Expr] * [String] -> Expr

   Derive(platform, builder, ins, outs) specifies the creation of new
   file objects (in paths declared by `outs') by the execution of a
   program `builder' on a platform `platform'.  This execution takes
   place in a file system state and in an environment given by `ins'.

     Str : String -> Expr

   A string constant.

     Tup : Expr * Expr -> Expr

   Tuples of expressions.

     Regular : String -> Content
     Directory : [(String, Content)] -> Content
     Hash : String -> Content

   File content, given either explicitly or implicitly through a cryptographic hash.

   The set of expressions in {\em $f$-normal form} is as follows:

     File : String * Content * [FExpr] -> FExpr

   These are completely evaluated Nix expressions.

   The set of expressions in {\em $d$-normal form} is as follows:

     File : String * Content * [DExpr] -> DExpr
     Derive : String * DExpr * [Tup] * [String] -> DExpr

     Tup : Str * DExpr -> Tup
     Tup : Str * Str -> Tup

     Str : String -> Str

   These are Nix expressions in which the file system result of Derive
   expressions has not yet been computed.  This is useful for, e.g.,
   querying dependencies.

*/

typedef ATerm Expr;


/* Evaluate an expression. */
Expr evalValue(Expr e);

/* Return a canonical textual representation of an expression. */
string printExpr(Expr e);

/* Perform variable substitution. */
Expr substExpr(string x, Expr rep, Expr e);

/* Hash an expression. */
Hash hashExpr(Expr e);


#endif /* !__EVAL_H */
