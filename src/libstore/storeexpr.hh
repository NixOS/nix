#ifndef __STOREEXPR_H
#define __STOREEXPR_H

#include "aterm.hh"
#include "store.hh"


/* Extension of derivations in the Nix store. */
const string drvExtension = ".drv";


/* Abstract syntax of derivations. */

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

/* Write a derivation to the Nix store, and return its path. */
Path writeDerivation(const Derivation & drv, const string & name);

/* Parse a derivation. */
Derivation parseDerivation(ATerm t);

/* Parse a derivation. */
ATerm unparseDerivation(const Derivation & drv);

/* Check whether a file name ends with the extensions for
   derivations. */
bool isDerivation(const string & fileName);


#endif /* !__STOREEXPR_H */
