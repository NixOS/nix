#ifndef __EVAL_H
#define __EVAL_H

#include <set>

extern "C" {
#include <aterm2.h>
}

#include "hash.hh"

using namespace std;


/* \section{Abstract syntax of Nix file system state expressions}

   A Nix file system state expression, or FState, describes a
   (partial) state of the file system.

     Path : Path * Content * [FState] -> FState

   Path(path, content, refs) specifies a file object (its full path
   and contents), along with all file objects referenced by it (that
   is, that it has pointers to).  We assume that all files are
   self-referential.  This prevents us from having to deal with
   cycles.

     Derive : String * Path * [FState] * Path * [(String, String)] -> FState

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

typedef set<string> StringSet;


/* Realise an fstate expression in the file system.  This requires
   execution of all Derive() nodes. */
FState realiseFState(FState fs, StringSet & paths);

/* Return the path of an fstate expression.  An empty string is
   returned if the term is not a valid fstate expression. (!!!) */
string fstatePath(FState fs);

/* Return the paths referenced by fstate expression. */
void fstateRefs(FState fs, StringSet & paths);

/* Return a canonical textual representation of an expression. */
string printTerm(ATerm t);

/* Throw an exception with an error message containing the given
   aterm. */
Error badTerm(const format & f, ATerm t);

/* Hash an aterm. */
Hash hashTerm(ATerm t);

FState hash2fstate(Hash hash);

/* Read an aterm from disk, given its hash. */
ATerm termFromHash(const Hash & hash, string * p = 0);

/* Write an aterm to the Nix store directory, and return its hash. */
Hash writeTerm(ATerm t, const string & suffix, string * p = 0);


#endif /* !__EVAL_H */
