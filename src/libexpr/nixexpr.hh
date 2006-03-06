#ifndef __NIXEXPR_H
#define __NIXEXPR_H

#include <map>

#include <aterm2.h>

#include "util.hh"


/* Nix expressions are represented as ATerms.  The maximal sharing
   property of the ATerm library allows us to implement caching of
   normals forms efficiently. */
typedef ATerm Expr;

typedef ATerm Pos;


/* Mappings from ATerms to ATerms.  This is just a wrapper around
   ATerm tables. */
class ATermMap
{
private:
    unsigned int maxLoadPct;
    ATermTable table;
    
public:
    ATermMap(unsigned int initialSize = 64, unsigned int maxLoadPct = 75);
    ATermMap(const ATermMap & map);
    ~ATermMap();

    ATermMap & operator = (const ATermMap & map);
        
    void set(ATerm key, ATerm value);
    void set(const string & key, ATerm value);

    ATerm get(ATerm key) const;
    ATerm get(const string & key) const;

    void remove(ATerm key);
    void remove(const string & key);

    ATermList keys() const;

    void add(const ATermMap & map);
    
    void reset();

private:
    void add(const ATermMap & map, ATermList & keys);

    void free();
    void copy(const ATermMap & map);
};


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
Expr substitute(const ATermMap & subs, Expr e);

/* Check whether all variables are defined in the given expression.
   Throw an exception if this isn't the case. */
void checkVarDefs(const ATermMap & def, Expr e);

/* Create an expression representing a boolean. */
Expr makeBool(bool b);


#endif /* !__NIXEXPR_H */
