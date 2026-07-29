#include "nix/store/derivations.hh"
#include "nix/store/local-store.hh"
#include "nix/store/pathlocks.hh"
#include "nix/util/util.hh"

#include "pathlocks-internal.hh"

#include <cerrno>
#include <cstdlib>

namespace nix {

std::set<std::filesystem::path> getDerivationOutputLockPaths(
    LocalStore & localStore, const StorePath & drvPath, const DerivationOutputsAndOptPaths & outputsAndOptPaths)
{
    std::set<std::filesystem::path> lockPaths;
    for (auto & [outputName, output] : outputsAndOptPaths) {
        if (auto & outputPath = output.second)
            lockPaths.insert(localStore.toRealPath(*outputPath));
        else {
            auto lockPath = localStore.toRealPath(drvPath);
            lockPath += "." + outputName;
            lockPaths.insert(std::move(lockPath));
        }
    }
    return lockPaths;
}

PathLocks::PathLocks()
    : deletePaths(false)
{
}

PathLocks::PathLocks(
    const std::set<std::filesystem::path> & paths, const std::string & waitMsg, LockOwnerTracking trackOwner)
    : deletePaths(false)
{
    lockPaths(paths, waitMsg, true, trackOwner);
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
