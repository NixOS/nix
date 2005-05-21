#ifndef __STORE_H
#define __STORE_H

#include <string>
#include <map>

#include "hash.hh"
#include "db.hh"

using namespace std;


struct PathHash
{
private:
    string rep;
public:
    PathHash();
    PathHash(const Hash & h);
    string toString() const;
    bool PathHash::isNull() const;
    bool operator ==(const PathHash & hash2) const;
    bool operator <(const PathHash & hash2) const;
};


/* Add the contents of the specified path to the Nix store.  Any
   occurence of the representation of `selfHash' (if not empty) is
   rewritten to the hash of the new store path. */
Path addToStore(const Path & srcPath, const PathHash & selfHash);


/* Rewrite a set of hashes in the given path. */
typedef map<PathHash, PathHash> HashRewrites;
//Path rewriteHashes(const Path & srcPath, HashRewrites rewrites);


#endif /* !__STORE_H */
