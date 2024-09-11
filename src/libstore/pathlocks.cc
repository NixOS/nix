#include "pathlocks.hh"
#include "util.hh"
#include "sync.hh"
#include "signals.hh"

#include <cerrno>
#include <cstdlib>


namespace nix {

PathLocks::PathLocks()
    : deletePaths(false)
{
}


PathLocks::PathLocks(const PathSet & paths, const std::string & waitMsg)
    : deletePaths(false)
{
    lockPaths(paths, waitMsg);
}


PathLocks::~PathLocks()
{
    try {
        unlock();
    } catch (...) {
        ignoreException();
    }
}


void PathLocks::setDeletion(bool deletePaths)
{
    this->deletePaths = deletePaths;
}


}
