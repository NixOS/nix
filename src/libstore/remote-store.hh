#ifndef __REMOTE_STORE_H
#define __REMOTE_STORE_H

#include <string>

#include "store-api.hh"


namespace nix {


class Pipe;
class Pid;
struct FdSink;
struct FdSource;


class RemoteStore : public StoreAPI
{
public:

    RemoteStore();

    ~RemoteStore();
    
    /* Implementations of abstract store API methods. */
    
    bool isValidPath(const Path & path);

    Substitutes querySubstitutes(const Path & srcPath);

    Hash queryPathHash(const Path & path);

    void queryReferences(const Path & storePath,
        PathSet & references);

    void queryReferrers(const Path & storePath,
        PathSet & referrers);

    Path addToStore(const Path & srcPath);

    Path addToStoreFixed(bool recursive, string hashAlgo,
        const Path & srcPath);

    Path addTextToStore(const string & suffix, const string & s,
        const PathSet & references);

    void buildDerivations(const PathSet & drvPaths);

    void ensurePath(const Path & storePath);

private:
    Pipe toChild;
    Pipe fromChild;
    FdSink to;
    FdSource from;
    Pid child;
};


}


#endif /* !__REMOTE_STORE_H */
