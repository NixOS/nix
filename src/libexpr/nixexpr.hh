#ifndef __NIXEXPR_H
#define __NIXEXPR_H

#include <map>

#include "aterm-map.hh"
#include "types.hh"


namespace nix {


MakeError(EvalError, Error)
MakeError(ParseError, Error)
MakeError(AssertionError, EvalError)
MakeError(ThrownError, AssertionError)
MakeError(Abort, EvalError)
MakeError(TypeError, EvalError)


/* Nix expressions are represented as ATerms.  The maximal sharing
   property of the ATerm library allows us to implement caching of
   normals forms efficiently. */
typedef ATerm Expr;
typedef ATerm DefaultValue;
typedef ATerm Pos;
typedef ATerm Pattern;
typedef ATerm ATermBool;


/* A STL vector of ATerms.  Should be used with great care since it's
   stored on the heap, and the elements are therefore not roots to the
   ATerm garbage collector. */
typedef vector<ATerm> ATermVector;


/* Show a position. */
string showPos(ATerm pos);


/* Generic bottomup traversal over ATerms.  The traversal first
   recursively descends into subterms, and then applies the given term
   function to the resulting term. */
struct TermFun
{
    virtual ~TermFun() { }
    virtual ATerm operator () (ATerm e) = 0;
};
ATerm bottomupRewrite(TermFun & f, ATerm e);


/* Create an attribute set expression from an Attrs value. */
Expr makeAttrs(const ATermMap & attrs);


/* Check whether all variables are defined in the given expression.
   Throw an exception if this isn't the case. */
void checkVarDefs(const ATermMap & def, Expr e);


/* Manipulation of Str() nodes.  Note: matchStr() does not clear
   context!  */
bool matchStr(Expr e, string & s, PathSet & context);

Expr makeStr(const string & s, const PathSet & context = PathSet());


}


#endif /* !__NIXEXPR_H */
