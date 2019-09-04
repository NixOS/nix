#pragma once

#include "types.hh"
#include "hash.hh"
#include "store-api.hh"

#include <map>


namespace nix {


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
    void parseHashInfo(bool & recursive, Hash & hash) const;
};

typedef std::map<string, DerivationOutput> DerivationOutputs;

/* For inputs that are sub-derivations, we specify exactly which
   output IDs we are interested in. */
typedef std::map<Path, StringSet> DerivationInputs;

typedef std::map<string, string> StringPairs;

struct BasicDerivation
{
    DerivationOutputs outputs; /* keyed on symbolic IDs */
    PathSet inputSrcs; /* inputs that are sources */
    string platform;
    Path builder;
    Strings args;
    StringPairs env;

    virtual ~BasicDerivation() { };

    /* Return the path corresponding to the output identifier `id' in
       the given derivation. */
    Path findOutput(const string & id) const;

    bool isBuiltin() const;

    /* Return true iff this is a fixed-output derivation. */
    bool isFixedOutput() const;

    /* Return the output paths of a derivation. */
    PathSet outputPaths() const;

};

struct Derivation : BasicDerivation
{
    DerivationInputs inputDrvs; /* inputs that are sub-derivations */

    /* Print a derivation. */
    std::string unparse() const;
};


class Store;


/* Write a derivation to the Nix store, and return its path. */
Path writeDerivation(ref<Store> store,
    const Derivation & drv, const string & name, RepairFlag repair = NoRepair);

/* Read a derivation from a file. */
Derivation readDerivation(const Path & drvPath);

/* Check whether a file name ends with the extension for
   derivations. */
bool isDerivation(const string & fileName);

Hash hashDerivationModulo(Store & store, Derivation drv);

/* Memoisation of hashDerivationModulo(). */
typedef std::map<Path, Hash> DrvHashes;

extern DrvHashes drvHashes; // FIXME: global, not thread-safe

/* Split a string specifying a derivation and a set of outputs
   (/nix/store/hash-foo!out1,out2,...) into the derivation path and
   the outputs. */
typedef std::pair<string, std::set<string> > DrvPathWithOutputs;
DrvPathWithOutputs parseDrvPathWithOutputs(const string & s);

Path makeDrvPathWithOutputs(const Path & drvPath, const std::set<string> & outputs);

bool wantOutput(const string & output, const std::set<string> & wanted);

struct Source;
struct Sink;

Source & readDerivation(Source & in, Store & store, BasicDerivation & drv);
Sink & operator << (Sink & out, const BasicDerivation & drv);

std::string hashPlaceholder(const std::string & outputName);

}
