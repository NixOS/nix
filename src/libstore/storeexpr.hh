#ifndef __STOREEXPR_H
#define __STOREEXPR_H

#include "aterm.hh"
#include "store.hh"


/* Abstract syntax of store expressions. */

struct ClosureElem
{
    PathSet refs;
};

typedef map<Path, ClosureElem> ClosureElems;

struct Closure
{
    PathSet roots;
    ClosureElems elems;
};

typedef map<string, string> StringPairs;

struct Derivation
{
    PathSet outputs;
    PathSet inputs; /* Store expressions, not actual inputs */
    string platform;
    Path builder;
    Strings args;
    StringPairs env;
};

struct StoreExpr
{
    enum { neClosure, neDerivation } type;
    Closure closure;
    Derivation derivation;
};


/* Hash an aterm. */
Hash hashTerm(ATerm t);

/* Write an aterm to the Nix store directory, and return its path. */
Path writeTerm(ATerm t, const string & suffix);

/* Parse a store expression. */
StoreExpr parseStoreExpr(ATerm t);

/* Parse a store expression. */
ATerm unparseStoreExpr(const StoreExpr & ne);


#endif /* !__STOREEXPR_H */
