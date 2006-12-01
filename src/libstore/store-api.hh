#ifndef __STOREAPI_H
#define __STOREAPI_H

#include <string>

#include <boost/shared_ptr.hpp>

#include "hash.hh"


namespace nix {


/* A substitute is a program invocation that constructs some store
   path (typically by fetching it from somewhere, e.g., from the
   network). */
struct Substitute
{       
    /* The derivation that built this store path (empty if none). */
    Path deriver;
    
    /* Program to be executed to create the store path.  Must be in
       the output path of `storeExpr'. */
    Path program;

    /* Extra arguments to be passed to the program (the first argument
       is the store path to be substituted). */
    Strings args;

    bool operator == (const Substitute & sub) const;
};

typedef list<Substitute> Substitutes;


class StoreAPI 
{
public:

    virtual ~StoreAPI() { }

    /* Checks whether a path is valid. */ 
    virtual bool isValidPath(const Path & path) = 0;

    /* Return the substitutes for the given path. */
    virtual Substitutes querySubstitutes(const Path & path) = 0;

    /* More efficient variant if we just want to know if a path has
       substitutes. */
    virtual bool hasSubstitutes(const Path & path);

    /* Queries the hash of a valid path. */ 
    virtual Hash queryPathHash(const Path & path) = 0;

    /* Queries the set of outgoing FS references for a store path.
       The result is not cleared. */
    virtual void queryReferences(const Path & path,
        PathSet & references) = 0;

    /* Queries the set of incoming FS references for a store path.
       The result is not cleared. */
    virtual void queryReferrers(const Path & path,
        PathSet & referrers) = 0;

    /* Copy the contents of a path to the store and register the
       validity the resulting path.  The resulting path is
       returned. */
    virtual Path addToStore(const Path & srcPath) = 0;

    /* Like addToStore(), but for pre-adding the outputs of
       fixed-output derivations. */
    virtual Path addToStoreFixed(bool recursive, string hashAlgo,
        const Path & srcPath) = 0;

    /* Like addToStore, but the contents written to the output path is
       a regular file containing the given string. */
    virtual Path addTextToStore(const string & suffix, const string & s,
        const PathSet & references) = 0;

    /* Ensure that the output paths of the derivation are valid.  If
       they are already valid, this is a no-op.  Otherwise, validity
       can be reached in two ways.  First, if the output paths have
       substitutes, then those can be used.  Second, the output paths
       can be created by running the builder, after recursively
       building any sub-derivations. */
    virtual void buildDerivations(const PathSet & drvPaths) = 0;

    /* Ensure that a path is valid.  If it is not currently valid, it
       may be made valid by running a substitute (if defined for the
       path). */
    virtual void ensurePath(const Path & path) = 0;
};


/* !!! These should be part of the store API, I guess. */

/* Throw an exception if `path' is not directly in the Nix store. */
void assertStorePath(const Path & path);

bool isInStore(const Path & path);
bool isStorePath(const Path & path);

void checkStoreName(const string & name);

/* Chop off the parts after the top-level store name, e.g.,
   /nix/store/abcd-foo/bar => /nix/store/abcd-foo. */
Path toStorePath(const Path & path);


/* Constructs a unique store path name. */
Path makeStorePath(const string & type,
    const Hash & hash, const string & suffix);
    
Path makeFixedOutputPath(bool recursive,
    string hashAlgo, Hash hash, string name);


/* This is the preparatory part of addToStore() and addToStoreFixed();
   it computes the store path to which srcPath is to be copied.
   Returns the store path and the cryptographic hash of the
   contents of srcPath. */
std::pair<Path, Hash> computeStorePathForPath(bool fixed, bool recursive,
    string hashAlgo, const Path & srcPath);

/* Preparatory part of addTextToStore().

   !!! Computation of the path should take the references given to
   addTextToStore() into account, otherwise we have a (relatively
   minor) security hole: a caller can register a source file with
   bogus references.  If there are too many references, the path may
   not be garbage collected when it has to be (not really a problem,
   the caller could create a root anyway), or it may be garbage
   collected when it shouldn't be (more serious).

   Hashing the references would solve this (bogus references would
   simply yield a different store path, so other users wouldn't be
   affected), but it has some backwards compatibility issues (the
   hashing scheme changes), so I'm not doing that for now. */
Path computeStorePathForText(const string & suffix, const string & s);


/* For now, there is a single global store API object, but we'll
   purify that in the future. */
extern boost::shared_ptr<StoreAPI> store;


/* Factory method: open the Nix database, either through the local or
   remote implementation. */
boost::shared_ptr<StoreAPI> openStore(bool reserveSpace = true);


}


#endif /* !__STOREAPI_H */
