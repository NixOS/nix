#pragma once

#include "nix/store/build/derivation-builder.hh"
#include "nix/store/local-settings.hh"
#include "nix/util/error.hh"

#include <filesystem>
#include <functional>

namespace nix {

class LocalStore;
struct UserLock;

struct NotDeterministic : BuildError
{
    NotDeterministic(auto &&... args)
        : BuildError(BuildResult::Failure::NotDeterministic, args...)
    {
        isNonDeterministic = true;
    }
};

void handleDiffHook(
    const Path & diffHook,
    uid_t uid,
    uid_t gid,
    const std::filesystem::path & tryA,
    const std::filesystem::path & tryB,
    const std::filesystem::path & drvPath,
    const std::filesystem::path & tmpDir);

void rethrowExceptionAsError();

void handleChildException(bool sendException);

void checkNotWorldWritable(std::filesystem::path path);

void movePath(const std::filesystem::path & src, const std::filesystem::path & dst);

void replaceValidPath(const std::filesystem::path & storePath, const std::filesystem::path & tmpPath);

/**
 * Register build outputs in the store. Shared by all derivation builder
 * implementations.
 *
 * @param realPathInHost Maps a store path string to its actual filesystem
 *   location on the host. For sandboxed (chroot) builders this resolves
 *   inside the chroot; for others it calls `store.toRealPath()`.
 */
SingleDrvOutputs registerOutputs(
    LocalStore & store,
    const LocalSettings & localSettings,
    const DerivationBuilderParams & params,
    const StorePathSet & addedPaths,
    const std::map<std::string, StorePath> & scratchOutputs,
    StringMap & outputRewrites,
    UserLock * buildUser,
    const std::filesystem::path & tmpDir,
    std::function<std::filesystem::path(const std::string &)> realPathInHost);

} // namespace nix
