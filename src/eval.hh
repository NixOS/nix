#ifndef __EVAL_H
#define __EVAL_H

extern "C" {
#include <aterm2.h>
}

#include "hash.hh"

using namespace std;


/* \section{Abstract syntax of Nix expressions}

   An expression describes a (partial) state of the file system in a
   referentially transparent way.  The operational effect of
   evaluating an expression is that the state described by the
   expression is realised.

     File : Path * Content * [Expr] -> Expr

   File(path, content, refs) specifies a file object (its full path
   and contents), along with all file objects referenced by it (that
   is, that it has pointers to).  We assume that all files are
   self-referential.  This prevents us from having to deal with
   cycles.

     Derive : String * Path * [Expr] * [Expr] * [Expr] -> Expr

   Derive(platform, builder, ins, outs, env) specifies the creation of
   new file objects (in paths declared by `outs') by the execution of
   a program `builder' on a platform `platform'.  This execution takes
   place in a file system state given by `ins'.  `env' specifies a
   mapping of strings to strings.

     Str : String -> Expr

   A string constant.

     Tup : Expr * Expr -> Expr

   Tuples of expressions.

     [ !!! NOT IMPLEMENTED 
       Regular : String -> Content
       Directory : [(String, Content)] -> Content
       (this complicates unambiguous normalisation)
     ]
     CHash : Hash -> Content

   File content, given either in situ, or through an external reference
   to the file system or url-space decorated with a hash to preserve purity.

   DISCUSSION: the idea is that a Regular/Directory is interchangeable
   with its CHash.  This would appear to break referential
   transparency, e.g., Derive(..., ..., [...CHash(h)...], ...) can
   only be reduced in a context were the Regular/Directory equivalent
   of Hash(h) is known.  However, CHash should be viewed strictly as a
   shorthand; that is, when we export an expression containing a
   CHash, we should also export the file object referenced by that
   CHash.


   \section{Reduction rules}

   ...


   \section{Normals forms}

   An expression is in {\em weak head normal form} if it is a lambda,
   a string or boolean value, or a File or Derive value.

   An expression is in {\em $d$-normal form} if it matches the
   signature FExpr:

     File : String * Content * [DExpr] -> DExpr
     Derive : String * Path * [Tup] * [Tup2] -> DExpr

     Tup : Str * DExpr -> Tup
     Tup : Str * Str -> Tup

     Tup : Str * Str -> Tup2

     Str : String -> Str

   These are Nix expressions in which the file system result of Derive
   expressions has not yet been computed.  This is useful for, e.g.,
   querying dependencies.

   An expression is in {\em $f$-normal form} if it matches the
   signature FExpr:

     File : String * Content * [FExpr] -> FExpr

   These are completely evaluated Nix expressions.

*/

typedef ATerm Expr;
typedef ATerm Content;


/* Expression normalisation. */
Expr whNormalise(Expr e);
Expr dNormalise(Expr e);
Expr fNormalise(Expr e);

/* Realise a $f$-normalised expression in the file system. */
void realise(Expr e);

/* Return a canonical textual representation of an expression. */
string printExpr(Expr e);

/* Perform variable substitution. */
Expr substExpr(string x, Expr rep, Expr e);

/* Hash an expression. */
Hash hashExpr(Expr e);


#endif /* !__EVAL_H */
