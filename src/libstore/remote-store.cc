#include "serialise.hh"
#include "util.hh"
#include "remote-store.hh"

#include <iostream>
#include <unistd.h>


namespace nix {


RemoteStore::RemoteStore()
{
    toChild.create();
    fromChild.create();


    /* Start the worker. */
    string worker = "nix-worker";

    child = fork();
    
    switch (child) {
        
    case -1:
        throw SysError("unable to fork");

    case 0:
        try { /* child */

            fromChild.readSide.close();
            if (dup2(fromChild.writeSide, STDOUT_FILENO) == -1)
                throw SysError("dupping write side");

            toChild.writeSide.close();
            if (dup2(toChild.readSide, STDIN_FILENO) == -1)
                throw SysError("dupping read side");

            execlp(worker.c_str(), worker.c_str(),
                "-vvv", "--slave", NULL);
            
            throw SysError(format("executing `%1%'") % worker);
            
        } catch (std::exception & e) {
            std::cerr << format("child error: %1%\n") % e.what();
        }
        quickExit(1);
    }

    fromChild.writeSide.close();
    toChild.readSide.close();

    from.fd = fromChild.readSide;
    to.fd = toChild.writeSide;

    
    /* Send the magic greeting, check for the reply. */
    writeInt(0x6e697864, to);
    
    unsigned int magic = readInt(from);
    if (magic != 0x6478696e) throw Error("protocol mismatch");
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
