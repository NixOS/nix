#ifndef __FSTATE_H
#define __FSTATE_H

extern "C" {
#include <aterm2.h>
}

#include "store.hh"


/* Abstract syntax of Nix expressions. */

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
    PathSet inputs; /* Nix expressions, not actual inputs */
    string platform;
    Path builder;
    Strings args;
    StringPairs env;
};

struct NixExpr
{
    enum { neClosure, neDerivation } type;
    Closure closure;
    Derivation derivation;
};


/* Return a canonical textual representation of an expression. */
string printTerm(ATerm t);

/* Throw an exception with an error message containing the given
   aterm. */
Error badTerm(const format & f, ATerm t);

/* Hash an aterm. */
Hash hashTerm(ATerm t);

/* Write an aterm to the Nix store directory, and return its path. */
Path writeTerm(ATerm t, const string & suffix);

/* Parse a Nix expression. */
NixExpr parseNixExpr(ATerm t);

/* Parse a Nix expression. */
ATerm unparseNixExpr(const NixExpr & ne);


#endif /* !__FSTATE_H */
