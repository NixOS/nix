#ifndef __NIXEXPR_H
#define __NIXEXPR_H

#include <map>

#include "aterm-map.hh"
#include "types.hh"


namespace nix {


MakeError(EvalError, Error)
MakeError(AssertionError, EvalError)
MakeError(Abort, EvalError)
MakeError(TypeError, EvalError)


/* Nix expressions are represented as ATerms.  The maximal sharing
   property of the ATerm library allows us to implement caching of
   normals forms efficiently. */
typedef ATerm Expr;

typedef ATerm DefaultValue;
typedef ATerm ValidValues;

typedef ATerm Pos;


/* A STL vector of ATerms.  Should be used with great care since it's
   stored on the heap, and the elements are therefore not roots to the
   ATerm garbage collector. */
typedef vector<ATerm> ATermVector;


/* A substitution is a linked list of ATermMaps that map names to
   identifiers.  We use a list of ATermMaps rather than a single to
   make it easy to grow or shrink a substitution when entering a
   scope. */
struct Substitution
{
    ATermMap * map;
    const Substitution * prev;

    Substitution(const Substitution * prev, ATermMap * map)
    {
        this->prev = prev;
        this->map = map;
    }
    
    Expr lookup(Expr name) const
    {
        Expr x;
        for (const Substitution * s(this); s; s = s->prev)
            if ((x = s->map->get(name))) return x;
        return 0;
    }
};


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


/* Query all attributes in an attribute set expression.  The
   expression must be in normal form. */
void queryAllAttrs(Expr e, ATermMap & attrs, bool withPos = false);

/* Query a specific attribute from an attribute set expression.  The
   expression must be in normal form. */
Expr queryAttr(Expr e, const string & name);
Expr queryAttr(Expr e, const string & name, ATerm & pos);

/* Create an attribute set expression from an Attrs value. */
Expr makeAttrs(const ATermMap & attrs);


/* Perform a set of substitutions on an expression. */
Expr substitute(const Substitution & subs, Expr e);


/* Check whether all variables are defined in the given expression.
   Throw an exception if this isn't the case. */
void checkVarDefs(const ATermMap & def, Expr e);


/* Canonicalise a Nix expression by sorting attributes and removing
   location information. */
Expr canonicaliseExpr(Expr e);


/* Create an expression representing a boolean. */
Expr makeBool(bool b);


/* Manipulation of Str() nodes.  Note: matchStr() does not clear
   context!  */
bool matchStr(Expr e, string & s, PathSet & context);

Expr makeStr(const string & s, const PathSet & context = PathSet());


/* Showing types, values. */
string showType(Expr e);

string showValue(Expr e);

 
}


#endif /* !__NIXEXPR_H */
