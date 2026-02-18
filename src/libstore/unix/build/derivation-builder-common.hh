#pragma once

#include "nix/store/build/derivation-builder.hh"
#include "nix/store/local-settings.hh"

#include <filesystem>
#include <functional>
#include <thread>
#include <tuple>
#include <vector>

namespace nix {

class LocalStore;
class Pid;
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
    UserLock * buildUser,
    const std::filesystem::path & tmpDir,
    std::function<std::filesystem::path(const std::string &)> realPathInHost);

/**
 * Change ownership of a path to the build user. No-op if no build user.
 */
void chownToBuilder(UserLock * buildUser, const std::filesystem::path & path);

/**
 * Change ownership of an open file descriptor to the build user. No-op
 * if no build user.
 */
void chownToBuilder(UserLock * buildUser, int fd, const std::filesystem::path & path);

/**
 * Write a file into the builder's temporary directory and chown it.
 */
void writeBuilderFile(
    UserLock * buildUser,
    const std::filesystem::path & tmpDir,
    int tmpDirFd,
    const std::string & name,
    std::string_view contents);

/**
 * Set up the build environment variables.
 *
 * @param tmpDirInSandbox The path to the build temporary directory as
 *   seen from inside the sandbox.
 */
StringMap initEnv(
    const std::string & storeDir,
    const DerivationBuilderParams & params,
    const StringMap & inputRewrites,
    const DerivationType & derivationType,
    const LocalSettings & localSettings,
    const std::filesystem::path & tmpDirInSandbox,
    UserLock * buildUser,
    const std::filesystem::path & tmpDir,
    int tmpDirFd);

/**
 * Compute scratch output paths and set up hash rewrites for each output.
 * Returns {scratchOutputs, inputRewrites, redirectedOutputs}.
 */
std::tuple<OutputPathMap, StringMap, std::map<StorePath, StorePath>> computeScratchOutputs(
    LocalStore & store,
    const DerivationBuilderParams & params,
    bool needsHashRewrite);

/**
 * Shut down the recursive Nix daemon socket, thread, and worker threads.
 */
void stopDaemon(
    AutoCloseFD & daemonSocket,
    std::thread & daemonThread,
    std::vector<std::thread> & daemonWorkerThreads);

/**
 * Read and process sandbox setup messages from the builder child process.
 * Waits for the "\2" ready signal, handles "\1" error reports.
 */
void processSandboxSetupMessages(
    AutoCloseFD & builderOut,
    Pid & pid,
    const Store & store,
    const StorePath & drvPath);

/**
 * Set up the recursive Nix daemon for the builder, including socket,
 * chown, accept loop, and environment variable.
 */
void setupRecursiveNixDaemon(
    LocalStore & store,
    DerivationBuilder & builder,
    const DerivationBuilderParams & params,
    StorePathSet & addedPaths,
    StringMap & env,
    const std::filesystem::path & tmpDir,
    const std::filesystem::path & tmpDirInSandbox,
    AutoCloseFD & daemonSocket,
    std::thread & daemonThread,
    std::vector<std::thread> & daemonWorkerThreads,
    UserLock * buildUser);

/**
 * Log chatty builder info (builder path, args, env vars).
 */
void logBuilderInfo(const BasicDerivation & drv);

/**
 * Set up the PTY master. On platforms where grantpt is needed when there
 * is no build user (macOS), pass `grantOnNoBuildUser = true`.
 */
void setupPTYMaster(
    AutoCloseFD & builderOut,
    UserLock * buildUser,
    bool grantOnNoBuildUser = false);

/**
 * Set up the PTY slave in the child process: open, configure raw mode,
 * dup2 to stderr.
 */
void setupPTYSlave(int masterFd);

/**
 * Pre-resolve AWS credentials for S3 URLs in `builtin:fetchurl`.
 * Returns nullopt if not applicable or on error.
 */
#if NIX_WITH_AWS_AUTH
struct AwsCredentials;
std::optional<AwsCredentials> preResolveAwsCredentials(const BasicDerivation & drv);
#endif

/**
 * Set up the `BuiltinBuilderContext` with fetchurl-specific data (netrc, caFile).
 */
struct BuiltinBuilderContext;
void setupBuiltinFetchurlContext(
    BuiltinBuilderContext & ctx,
    const BasicDerivation & drv);

/**
 * Run a builtin builder. Does not return on success (calls `_exit(0)`).
 * On failure, writes error to stderr and calls `_exit(1)`.
 */
[[noreturn]] void runBuiltinBuilder(
    BuiltinBuilderContext & ctx,
    const BasicDerivation & drv,
    const OutputPathMap & scratchOutputs,
    Store & store);

/**
 * Drop privileges to the build user (setgroups, setgid, setuid).
 * Preserves the death signal across the uid change.
 */
void dropPrivileges(UserLock & buildUser);

/**
 * Build the execve argument and environment arrays, then exec the builder.
 * Does not return.
 */
[[noreturn]] void execBuilder(
    const BasicDerivation & drv,
    const StringMap & inputRewrites,
    const StringMap & env);

/**
 * Check whether the store or tmpdir is low on disk space.
 */
bool isDiskFull(LocalStore & store, const std::filesystem::path & tmpDir);

/**
 * Common first part of `unprepareBuild()`: kill the child, log,
 * update build result, close log file. Returns the exit status.
 */
int commonUnprepare(
    Pid & pid,
    const Store & store,
    const StorePath & drvPath,
    BuildResult & buildResult,
    DerivationBuilderCallbacks & miscMethods,
    AutoCloseFD & builderOut);

/**
 * Log CPU usage stats if available.
 */
void logCpuUsage(
    const Store & store,
    const StorePath & drvPath,
    const BuildResult & buildResult,
    int status);

/**
 * Common core of `cleanupBuild()`: delete redirected outputs if forced,
 * handle keepFailed, clean up tmpDir.
 */
void cleanupBuildCore(
    bool force,
    LocalStore & store,
    const std::map<StorePath, StorePath> & redirectedOutputs,
    const BasicDerivation & drv,
    std::filesystem::path & topTmpDir,
    std::filesystem::path & tmpDir);

/**
 * Validate impure host dependencies against allowed prefixes and add
 * them to `pathsInChroot`.
 */
void checkAndAddImpurePaths(
    PathsInChroot & pathsInChroot,
    const DerivationOptions<StorePath> & drvOptions,
    const Store & store,
    const StorePath & drvPath,
    const PathSet & allowedPrefixes);

/**
 * Parse pre-build hook output and add extra sandbox paths.
 */
void parsePreBuildHook(
    PathsInChroot & pathsInChroot,
    const std::string & hookOutput);

/**
 * Home directory path used by non-sandboxed and Darwin builders.
 */
extern const std::filesystem::path homeDir;

} // namespace nix
