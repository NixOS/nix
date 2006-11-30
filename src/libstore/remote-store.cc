#include "remote-store.hh"


namespace nix {


RemoteStore::RemoteStore()
{
    throw Error("not implemented");
}


RemoteStore::~RemoteStore()
{
}


bool RemoteStore::isValidPath(const Path & path)
{
    throw Error("not implemented");
}


Substitutes RemoteStore::querySubstitutes(const Path & srcPath)
{
    throw Error("not implemented");
}


Hash RemoteStore::queryPathHash(const Path & path)
{
    throw Error("not implemented");
}


void RemoteStore::queryReferences(const Path & storePath,
    PathSet & references)
{
    throw Error("not implemented");
}


void RemoteStore::queryReferrers(const Path & storePath,
    PathSet & referrers)
{
    throw Error("not implemented");
}


Path RemoteStore::addToStore(const Path & srcPath)
{
    throw Error("not implemented");
}


Path RemoteStore::addToStoreFixed(bool recursive, string hashAlgo,
    const Path & srcPath)
{
    throw Error("not implemented");
}


Path RemoteStore::addTextToStore(const string & suffix, const string & s,
    const PathSet & references)
{
    throw Error("not implemented");
}


void RemoteStore::buildDerivations(const PathSet & drvPaths)
{
    throw Error("not implemented");
}


void RemoteStore::ensurePath(const Path & storePath)
{
    throw Error("not implemented");
}


}
