#ifndef __FIXEXPR_H
#define __FIXEXPR_H

#include <aterm2.h>

#include "util.hh"


/* Fix expressions are represented as ATerms.  The maximal sharing
   property of the ATerm library allows us to implement caching of
   normals forms efficiently. */
typedef ATerm Expr;


/* Generic bottomup traversal over ATerms.  The traversal first
   recursively descends into subterms, and then applies the given term
   function to the resulting term. */

struct TermFun
{
    virtual ATerm operator () (ATerm e) = 0;
};

ATerm bottomupRewrite(TermFun & f, ATerm e);


#endif /* !__FIXEXPR_H */
