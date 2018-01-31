#pragma once

#include "types.hh"
#include "hash.hh"

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
    void parseHashInfo(bool & recursive, HashType & hashType, Hash & hash) const;
};

typedef std::map<string, DerivationOutput> DerivationOutputs;

/* For inputs that are sub-derivations, we specify exactly which
   output IDs we are interested in. */
typedef std::map<Path, StringSet> DerivationInputs;

typedef std::map<string, string> StringPairs;


/* A Derivation without its closure (sub-derivations).
   This is used to prevent having to send the closure of a drv file
   (which could be very large), e.g. for remote build operations. */
struct BasicDerivation
{
    /* Keyed on symbolic IDs, like `"out"` or `"doc"`. */
    DerivationOutputs outputs;

    /* Inputs that are sources. <- TODO what’s a source? */
    PathSet inputSrcs;

    /* The system architecture this derivation can be built on,
       as returned by `config/config.guess`, e.g. `"x86_64-linux"` */
    string platform;

    /* Program that executes the build. Either a path to a derivation
       or a script. */
    Path builder;

    /* Command line arguments passed to the builder. */
    Strings args;

    /* POSIX environment passed to the builder. */
    StringPairs env;

    virtual ~BasicDerivation() { };
};

/* Description of a build action, input for the builder. */
struct Derivation : BasicDerivation
{
    DerivationInputs inputDrvs; /* inputs that are sub-derivations */
};


class StoreAPI;


/* Write a derivation to the Nix store, and return its path. */
Path writeDerivation(StoreAPI & store,
    const Derivation & drv, const string & name, bool repair = false);

/* Read a derivation from a file. */
Derivation readDerivation(const Path & drvPath);

/* Print a derivation. */
string unparseDerivation(const Derivation & drv);

/* Check whether a file name ends with the extensions for
   derivations. */
bool isDerivation(const string & fileName);

/* Return true iff this is a fixed-output derivation. */
bool isFixedOutputDrv(const Derivation & drv);

Hash hashDerivationModulo(StoreAPI & store, Derivation drv);

/* Memoisation of hashDerivationModulo(). */
typedef std::map<Path, Hash> DrvHashes;

extern DrvHashes drvHashes;

/* Split a string specifying a derivation and a set of outputs
   (/nix/store/hash-foo!out1,out2,...) into the derivation path and
   the outputs. */
typedef std::pair<string, std::set<string> > DrvPathWithOutputs;
DrvPathWithOutputs parseDrvPathWithOutputs(const string & s);

Path makeDrvPathWithOutputs(const Path & drvPath, const std::set<string> & outputs);

bool wantOutput(const string & output, const std::set<string> & wanted);

PathSet outputPaths(const BasicDerivation & drv);

struct Source;
struct Sink;

Source & operator >> (Source & in, BasicDerivation & drv);
Sink & operator << (Sink & out, const BasicDerivation & drv);

}
