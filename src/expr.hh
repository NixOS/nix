#ifndef __FSTATE_H
#define __FSTATE_H

extern "C" {
#include <aterm2.h>
}

#include "store.hh"


/* Abstract syntax of Nix expressions. */

typedef list<FSId> FSIds;

struct ClosureElem
{
    FSId id;
    StringSet refs;
};

typedef map<string, ClosureElem> ClosureElems;

struct Closure
{
    StringSet roots;
    ClosureElems elems;
};

typedef map<string, FSId> DerivationOutputs;
typedef map<string, string> StringPairs;

struct Derivation
{
    DerivationOutputs outputs;
    FSIdSet inputs;
    string platform;
    string builder;
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

/* Read an aterm from disk, given its id. */
ATerm termFromId(const FSId & id);

/* Write an aterm to the Nix store directory, and return its hash. */
FSId writeTerm(ATerm t, const string & suffix, FSId id = FSId());

/* Parse a Nix expression. */
NixExpr parseNixExpr(ATerm t);

/* Parse a Nix expression. */
ATerm unparseNixExpr(const NixExpr & ne);


#endif /* !__FSTATE_H */
