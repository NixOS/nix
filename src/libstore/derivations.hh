#ifndef __DERIVATIONS_H
#define __DERIVATIONS_H

typedef struct _ATerm * ATerm;

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
};

struct DerivationStateOutput
{
    Path statepath;
    string hashAlgo;
    string hash;
    string enabled;
    string shared;
    string synchronization;
    DerivationStateOutput()
    {
    }
    DerivationStateOutput(Path statepath, string hashAlgo, string hash, string enabled, string shared, string synchronization)
    {
        this->statepath = statepath;
        this->hashAlgo = hashAlgo;
        this->hash = hash;
        this->enabled = enabled;
        this->shared = shared;
        this->synchronization = synchronization;
    }
};

struct DerivationStateOutputDir
{
    string path;
    string type;
    string interval;
    DerivationStateOutputDir()
    {
    }
    DerivationStateOutputDir(string path, string type, string interval)
    {
        this->path = path;
        this->type = type;
        this->interval = interval;
    }
    
    //sort function
    /*bool operator<(const DerivationStateOutputDir& a, const DerivationStateOutputDir& b) {
    	return a.path < b.path;
	} */
	bool operator<(const DerivationStateOutputDir& a) const { return path < a.path; }      
};


typedef std::map<string, DerivationOutput> DerivationOutputs;
typedef std::map<string, DerivationStateOutput> DerivationStateOutputs;
typedef std::map<string, DerivationStateOutputDir> DerivationStateOutputDirs;


/* For inputs that are sub-derivations, we specify exactly which
   output IDs we are interested in. */
typedef std::map<Path, StringSet> DerivationInputs;
typedef std::map<string, string> StringPairs;

struct Derivation
{
    DerivationOutputs outputs; /* keyed on symbolic IDs */
    DerivationStateOutputs stateOutputs; /*  */
    DerivationStateOutputDirs stateOutputDirs; /*  */    
    DerivationInputs inputDrvs; /* inputs that are sub-derivations */
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

 
}


#endif /* !__DERIVATIONS_H */
