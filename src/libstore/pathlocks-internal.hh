#pragma once
///@file

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>

#include "nix/store/derivations.hh"
#include "nix/util/error.hh"

namespace nix {

class LocalStore;

MakeError(MissingLockOwner, Error);

struct FileLockOwner
{
    uint64_t pid;
    uint64_t startTime = 0;
    uint64_t startTimeFraction = 0;

    bool operator==(const FileLockOwner &) const = default;
};

std::set<std::filesystem::path> getDerivationOutputLockPaths(
    LocalStore & localStore, const StorePath & drvPath, const DerivationOutputsAndOptPaths & outputsAndOptPaths);

std::optional<FileLockOwner> getFileLockOwner(const std::filesystem::path & path);

bool fileLockOwnerStillHoldsLock(const std::filesystem::path & path, const FileLockOwner & owner);

bool signalFileLockOwner(const std::filesystem::path & path, const FileLockOwner & owner, int signal);

} // namespace nix
