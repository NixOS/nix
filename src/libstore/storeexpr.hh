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

/*
struct Closure
{
    PathSet roots;
    ClosureElems elems;
};
*/


struct DerivationOutput
{
    Path path;
    string hashAlgo; /* hash used for expected hash computation */
    string hash; /* expected hash, may be null */
    DerivationOutput()
    {
    }
    DerivationOutput(Path path, string hashAlgo, string hash)
    {
        this->path = path;
        this->hashAlgo = hashAlgo;
        this->hash = hash;
    }
};

typedef map<string, DerivationOutput> DerivationOutputs;
typedef map<string, string> StringPairs;

struct Derivation
{
    DerivationOutputs outputs; /* keyed on symbolic IDs */
    PathSet inputDrvs; /* inputs that are sub-derivations */
    PathSet inputSrcs; /* inputs that are sources */
    string platform;
    Path builder;
    Strings args;
    StringPairs env;
};


/* Hash an aterm. */
Hash hashTerm(ATerm t);

/* Write an aterm to the Nix store directory, and return its path. */
Path writeTerm(ATerm t, const string & suffix);

/* Parse a store expression. */
Derivation parseDerivation(ATerm t);

/* Parse a store expression. */
ATerm unparseDerivation(const Derivation & drv);


#endif /* !__STOREEXPR_H */
