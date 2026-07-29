#include "nix/store/derivations.hh"
#include "nix/store/local-store.hh"
#include "nix/store/pathlocks.hh"
#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"

#include "../pathlocks-internal.hh"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <set>
#include <thread>
#include <unistd.h>

namespace nix {

static std::optional<uint64_t>
terminateLockOwner(const std::set<std::filesystem::path> & lockPaths, std::chrono::steady_clock::duration timeout)
{
    std::optional<FileLockOwner> owner;
    for (auto & lockPath : lockPaths) {
        auto candidate = getFileLockOwner(lockPath);
        if (!candidate)
            continue;
        if (owner && *candidate != *owner)
            throw Error("multiple processes hold output locks for the requested derivation");
        owner = candidate;
    }

    if (!owner)
        return std::nullopt;
    if (owner->pid == static_cast<uint64_t>(getpid()))
        throw Error("refusing to terminate this Nix process while it holds an output lock");

    bool signalled = false;
    for (auto & lockPath : lockPaths)
        if (signalFileLockOwner(lockPath, *owner, SIGTERM)) {
            signalled = true;
            break;
        }
    if (!signalled)
        return std::nullopt;

    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        checkInterrupt();
        if (std::ranges::none_of(
                lockPaths, [&](auto & lockPath) { return fileLockOwnerStillHoldsLock(lockPath, *owner); }))
            return owner->pid;
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            break;
        auto nextCheck = now + std::chrono::milliseconds(50);
        std::this_thread::sleep_until(nextCheck < deadline ? nextCheck : deadline);
    }

    throw Error("process %d did not release its output locks after being asked to terminate", owner->pid);
}

std::optional<uint64_t> LocalStore::killBuild(const StorePath & path)
{
#if !defined(__linux__) && !defined(__APPLE__)
    throw UsageError("build termination is not supported on this platform");
#endif

    std::set<std::filesystem::path> lockPaths;
    auto addLockPath = [&](std::filesystem::path path) {
        path += ".lock";
        lockPaths.insert(std::move(path));
    };

    addLockPath(toRealPath(path));
    if (path.isDerivation() && isValidPath(path)) {
        auto drv = readDerivation(path);
        for (auto & outputLockPath : getDerivationOutputLockPaths(*this, path, drv.outputsAndOptPaths(*this)))
            addLockPath(outputLockPath);
    }

    return terminateLockOwner(lockPaths, std::chrono::seconds(5));
}

} // namespace nix
