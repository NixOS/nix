#ifndef __EVAL_H
#define __EVAL_H

extern "C" {
#include <aterm2.h>
}

#include "hash.hh"

using namespace std;


/* \section{Abstract syntax of Nix file system state expressions}

   A Nix file system state expression, or FState, describes a
   (partial) state of the file system.

     File : Path * Content * [FState] -> FState

   File(path, content, refs) specifies a file object (its full path
   and contents), along with all file objects referenced by it (that
   is, that it has pointers to).  We assume that all files are
   self-referential.  This prevents us from having to deal with
   cycles.

     Derive : String * Path * [FState] * [Path] * [(String, String)] -> [FState]

   Derive(platform, builder, ins, outs, env) specifies the creation of
   new file objects (in paths declared by `outs') by the execution of
   a program `builder' on a platform `platform'.  This execution takes
   place in a file system state given by `ins'.  `env' specifies a
   mapping of strings to strings.

     [ !!! NOT IMPLEMENTED 
       Regular : String -> Content
       Directory : [(String, Content)] -> Content
       (this complicates unambiguous normalisation)
     ]
     CHash : Hash -> Content

   File content, given either in situ, or through an external reference
   to the file system or url-space decorated with a hash to preserve
   purity.

   A FState expression is in {\em $f$-normal form} if all Derive nodes
   have been reduced to File nodes.

   DISCUSSION: the idea is that a Regular/Directory is interchangeable
   with its CHash.  This would appear to break referential
   transparency, e.g., Derive(..., ..., [...CHash(h)...], ...) can
   only be reduced in a context were the Regular/Directory equivalent
   of Hash(h) is known.  However, CHash should be viewed strictly as a
   shorthand; that is, when we export an expression containing a
   CHash, we should also export the file object referenced by that
   CHash.

*/

typedef ATerm FState;
typedef ATerm Content;


/* Realise a $f$-normalised expression in the file system. */
void realiseFState(FState fs);

/* Return a canonical textual representation of an expression. */
string printTerm(ATerm t);

/* Hash an aterm. */
Hash hashTerm(ATerm t);


#endif /* !__EVAL_H */
