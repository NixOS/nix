#include "logging.hh"
#include "pathlocks.hh"

namespace nix {

bool PathLocks::lockPaths(const PathSet & _paths, const std::string & waitMsg, bool wait)
{
    return true;
}

void PathLocks::unlock()
{
    warn("PathLocks::unlock: not yet implemented");
}

}
