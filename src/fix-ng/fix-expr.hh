#ifndef __FIXEXPR_H
#define __FIXEXPR_H

#include <map>

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

/* Query all attributes in an attribute set expression.  The
   expression must be in normal form. */
typedef map<string, Expr> Attrs;
void queryAllAttrs(Expr e, Attrs & attrs);

/* Query a specific attribute from an attribute set expression.  The
   expression must be in normal form. */
Expr queryAttr(Expr e, const string & name);

/* Create an attribute set expression from an Attrs value. */
Expr makeAttrs(const Attrs & attrs);

/* Perform a set of substitutions on an expression. */
typedef map<string, Expr> Subs;
ATerm substitute(Subs & subs, ATerm e);


#endif /* !__FIXEXPR_H */
