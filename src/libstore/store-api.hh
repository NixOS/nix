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
    virtual Substitutes querySubstitutes(const Path & srcPath) = 0;

    /* Queries the hash of a valid path. */ 
    virtual Hash queryPathHash(const Path & path) = 0;

    /* Queries the set of outgoing FS references for a store path.
       The result is not cleared. */
    virtual void queryReferences(const Path & storePath,
        PathSet & references) = 0;

    /* Queries the set of incoming FS references for a store path.
       The result is not cleared. */
    virtual void queryReferrers(const Path & storePath,
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


/* For now, there is a single global store API object, but we'll
   purify that in the future. */
extern boost::shared_ptr<StoreAPI> store;


/* Factory method: open the Nix database, either through the local or
   remote implementation. */
boost::shared_ptr<StoreAPI> openStore(bool reserveSpace = true);



}


#endif /* !__STOREAPI_H */
