#include <algorithm>

#ifndef _WIN32
#  ifdef __APPLE__
#    include <sys/time.h>
#  else
#    include <sys/stat.h>
#  endif
#endif

#include "nix/store/machine-selection.hh"
#include "nix/store/globals.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/util/hash.hh"

namespace nix {

std::filesystem::path getCurrentLoadDir(Store & store)
{
    /* It would be more appropriate to use $XDG_RUNTIME_DIR, since
       that gets cleared on reboot, but it wouldn't work on macOS. */
    if (auto * localStore = dynamic_cast<LocalFSStore *>(&store))
        return localStore->config.stateDir.get() / "current-load";
    else
        return std::filesystem::path{settings.nixStateDir} / "current-load";
}

std::string escapeUri(std::string uri)
{
    std::replace(uri.begin(), uri.end(), '/', '_');
    return uri;
}

static AutoCloseFD openSlotLock(const std::filesystem::path & currentLoadDir, const Machine & m, uint64_t slot)
{
    return openLockFile(currentLoadDir / fmt("%s-%d", escapeUri(m.storeUri.render()), slot), true);
}

bool machineIsCandidate(const Machine & machine, const std::string & neededSystem, const StringSet & requiredFeatures)
{
    return machine.enabled && machine.systemSupported(neededSystem) && machine.allSupported(requiredFeatures)
           && machine.mandatoryMet(requiredFeatures);
}

bool machineIsBetter(const Machine & cand, uint64_t candLoad, const Machine & best, uint64_t bestLoad)
{
    if (candLoad / cand.speedFactor < bestLoad / best.speedFactor)
        return true;
    if (candLoad / cand.speedFactor == bestLoad / best.speedFactor) {
        if (cand.speedFactor > best.speedFactor)
            return true;
        if (cand.speedFactor == best.speedFactor)
            return candLoad < bestLoad;
    }
    return false;
}

std::variant<AcquiredMachine, NoMachine> acquireMachineSlot(
    const std::filesystem::path & currentLoadDir,
    Machines & machines,
    const std::string & neededSystem,
    const StringSet & requiredFeatures)
{
    AutoCloseFD lock = openLockFile(currentLoadDir / "main-lock", true);
    lockFile(lock.get(), ltWrite, true);

    bool rightType = false;

    Machine * bestMachine = nullptr;
    uint64_t bestLoad = 0;
    AutoCloseFD bestSlotLock;

    for (auto & m : machines) {
        debug("considering building on remote machine '%s'", m.storeUri.render());

        if (!machineIsCandidate(m, neededSystem, requiredFeatures))
            continue;

        rightType = true;

        AutoCloseFD free;
        uint64_t load = 0;
        for (uint64_t slot = 0; slot < m.maxJobs; ++slot) {
            auto slotLock = openSlotLock(currentLoadDir, m, slot);
            if (lockFile(slotLock.get(), ltWrite, false)) {
                if (!free) {
                    free = std::move(slotLock);
                }
            } else {
                ++load;
            }
        }
        if (!free) {
            continue;
        }
        if (!bestSlotLock || machineIsBetter(m, load, *bestMachine, bestLoad)) {
            bestLoad = load;
            bestSlotLock = std::move(free);
            bestMachine = &m;
        }
    }

    if (!bestSlotLock)
        return rightType ? NoMachine::AllBusy : NoMachine::WrongType;

#ifndef _WIN32
#  ifdef __APPLE__
    futimes(bestSlotLock.get(), NULL);
#  else
    futimens(bestSlotLock.get(), NULL);
#  endif
#endif

    return AcquiredMachine{
        .machine = bestMachine,
        .slotLock = std::move(bestSlotLock),
    };
}

AutoCloseFD openUploadLock(const std::filesystem::path & currentLoadDir, std::string_view storeUri)
{
    auto open = [&](std::string_view fileName) {
        return openLockFile(currentLoadDir / (escapeUri(std::string{fileName}) + ".upload-lock"), true);
    };
    try {
        return open(storeUri);
    } catch (SystemError & e) {
        if (!e.is(std::errc::filename_too_long))
            throw;
        /* Try again hashing the store URL so we have a shorter path. */
        auto h = hashString(HashAlgorithm::MD5, storeUri);
        return open(h.to_string(HashFormat::Base64, false));
    }
}

} // namespace nix
