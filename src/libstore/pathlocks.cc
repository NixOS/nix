#include "nix/store/pathlocks.hh"
#include "nix/util/util.hh"
#include "nix/util/sync.hh"
#include "nix/util/signals.hh"

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
        ignoreExceptionInDestructor();
    }
}

void PathLocks::setDeletion(bool deletePaths)
{
    this->deletePaths = deletePaths;
}

} // namespace nix
