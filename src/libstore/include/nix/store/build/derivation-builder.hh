#pragma once
///@file

#include <filesystem>
#include <nlohmann/json_fwd.hpp>

#include "nix/store/build-result.hh"
#include "nix/store/daemon.hh"
#include "nix/store/derivation-options.hh"
#include "nix/store/build/derivation-building-misc.hh"
#include "nix/store/derivations.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/util/processes.hh"
#include "nix/util/muxable-pipe.hh"
#include "nix/util/json-impls.hh"
#include "nix/store/restricted-store.hh"
#include "nix/store/build/derivation-env-desugar.hh"

namespace nix {

/**
 * Rethrow the current exception as a subclass of `Error`.
 */
void rethrowExceptionAsError();

/**
 * Send the current exception to the parent in the format expected by
 * `UnixDerivationBuilderImpl::processSandboxSetupMessages()`.
 */
void handleChildException(bool sendException);

/**
 * Denotes a build failure that stemmed from the builder exiting with a
 * failing exist status.
 */
struct BuilderFailureError final : CloneableError<BuilderFailureError, BuildError>
{
private:
    void anchor() override;

public:
    int builderStatus;

    std::string extraMsgAfter;

    BuilderFailureError(BuildResult::Failure::Status status, int builderStatus, std::string extraMsgAfter)
        : CloneableError{
            status,
              /* No message for now, because the caller will make for
                 us, with extra context */
              "",
          }
        , builderStatus{std::move(builderStatus)}
        , extraMsgAfter{std::move(extraMsgAfter)}
    {
    }
};

/**
 * Stuff we need to pass to initChild().
 */
struct ChrootPath
{
    std::filesystem::path source;
    bool optional = false;
};

void to_json(nlohmann::json & j, const ChrootPath & cp);
void from_json(const nlohmann::json & j, ChrootPath & cp);

typedef std::map<std::filesystem::path, ChrootPath> PathsInChroot; // maps target path to source path

/**
 * Parameters by (mostly) `const` reference for `DerivationBuilder`.
 */
struct DerivationBuilderParams
{
    /** The path of the derivation. */
    const StorePath & drvPath;

    BuildResult & buildResult;

    /**
     * The derivation stored at drvPath.
     */
    const BasicDerivation & drv;

    /**
     * The derivation options of `drv`.
     *
     * @todo this should be part of `Derivation`.
     */
    const DerivationOptions<StorePath> & drvOptions;

    // The remainder is state held during the build.

    /**
     * All input paths (that is, the union of FS closures of the
     * immediate input paths).
     */
    const StorePathSet & inputPaths;

    const std::map<std::string, InitialOutput> initialOutputs;

    const BuildMode & buildMode;

    /**
     * Extra paths we want to be in the chroot, regardless of the
     * derivation we are building.
     */
    PathsInChroot defaultPathsInChroot;

    /**
     * May be used to control various platform-specific functionality.
     *
     * For example, on Linux, the `kvm` system feature controls whether
     * `/dev/kvm` should be exposed to the builder within the sandbox.
     */
    StringSet systemFeatures;

    DesugaredEnv desugaredEnv;
};

/**
 * Callbacks that `DerivationBuilder` needs.
 */
struct DerivationBuilderCallbacks
{
    virtual ~DerivationBuilderCallbacks();

    /**
     * Open a log file and a pipe to it.
     */
    virtual void openLogFile() = 0;

    /**
     * Close the log file.
     */
    virtual void closeLogFile() = 0;

    /**
     * @todo this should be reworked
     */
    virtual void childTerminated() = 0;

    /**
     * Process a recursive Nix daemon connection, using a builder
     * that enforces the restrictions of the given context.
     */
    virtual void processDaemonConnection(
        ref<Store> store,
        FdSource && from,
        FdSink && to,
        RestrictionContext & context,
        daemon::RecursiveFlag recursiveFlag) = 0;
};

/**
 * The outcome of tearing down the build environment, from
 * `DerivationBuilder::unprepareBuild`.
 */
struct BuilderExit
{
    /**
     * The builder's exit status.
     */
    int status;

    /**
     * Whether the disk seemed full when the builder exited.
     */
    bool diskFull = false;
};

/**
 * This class represents the state for building locally.
 *
 * @todo Ideally, it would not be a class, but a single function.
 * However, besides the main entry point, there are a few more methods
 * which are externally called, and need to be gotten rid of. There are
 * also some virtual methods (either directly here or inherited from
 * `DerivationBuilderCallbacks`, a stop-gap) that represent outgoing
 * rather than incoming call edges that either should be removed, or
 * become (higher order) function parameters.
 */
struct DerivationBuilder : RestrictionContext
{
private:
    void anchor() override;

public:
    DerivationBuilder() = default;
    virtual ~DerivationBuilder() = default;

    /**
     * Where the builder's standard output/error is read from, set by
     * `startBuild`.
     *
     * The two platforms cannot share a type here: Unix hands out the
     * master side of a pseudoterminal, whereas @ref
     * nix::windows::MuxablePipePollState::iterate needs the pipe's
     * `OVERLAPPED` state and buffer, which a `Descriptor` does not
     * carry.
     */
#ifndef _WIN32
    AutoCloseFD builderOut;
#else
    MuxablePipe * builderOut = nullptr;
#endif

    /**
     * What the worker should wait on for this build's log.
     */
    MuxablePipePollState::CommChannel logChannel()
    {
#ifndef _WIN32
        return builderOut.get();
#else
        return builderOut;
#endif
    }

    /**
     * The descriptor a `ChildOutput` event for this build's log carries.
     */
    Descriptor logDescriptor()
    {
#ifndef _WIN32
        return builderOut.get();
#else
        return builderOut->readSide.get();
#endif
    }

    /**
     * Set up build environment / sandbox, acquiring resources (e.g.
     * locks as needed). After this is run, the builder should be
     * started.
     *
     * @returns logging pipe if successful, `std::nullopt` if we could
     * not acquire a build user. In that case, the caller must wait and
     * then try again.
     *
     * @note "success" just means that we were able to set up the environment
     * and start the build. The builder could have immediately exited with
     * failure, and that would still be considered a successful start.
     */
    virtual std::optional<Descriptor> startBuild() = 0;

    /**
     * Tear down build environment after the builder exits (either on
     * its own or if it is killed).
     *
     * @returns The builder's exit status and whether the disk seemed
     * full at exit time.
     */
    virtual BuilderExit unprepareBuild() = 0;

    /**
     * Check that the derivation outputs all exist and register them
     * as valid.
     *
     * Not used with `builder-rpc-v0`; see `checkSubmittedOutputs`.
     */
    virtual SingleDrvOutputs registerOutputs(LocalStore & store) = 0;

    /**
     * Check that the derivation outputs submitted by recursive-nix
     * exist and attach them to the derivation.
     *
     * Only used with `builder-rpc-v0`.
     */
    virtual SingleDrvOutputs checkSubmittedOutputs(LocalStore & store) = 0;

    /**
     * Delete the temporary directory, if we have one.
     *
     * @param force We know the build succeeded, so don't attempt to
     * preserve anything for debugging.
     */
    virtual void cleanupBuild(bool force) = 0;

    /**
     * Forcibly kill the child process, if any.
     *
     * @returns whether the child was still alive and needed to be
     * killed.
     */
    virtual bool killChild() = 0;
};

/**
 * Run a callback that may change process credentials (setuid, setgid, etc.)
 * while preserving the parent-death signal.
 *
 * The parent-death signal setting is cleared by the Linux kernel upon changes
 * to EUID, EGID.
 *
 * @note Does nothing on non-Linux systems.
 * @see man PR_SET_PDEATHSIG
 * @see https://github.com/golang/go/issues/9686
 */
void preserveDeathSignal(fun<void()> setCredentials);

struct ExternalBuilder
{
    StringSet systems;
    std::filesystem::path program;
    std::vector<std::string> args;
};

struct LocalSettings;

/**
 * This type exists to aid with FFI: we cannot make a full `LocalStore`
 * with everything (including building, which uses this!) from FFI, but
 * we do have a chance of making something that just has the methods we
 * actually need from `LocalStore`.
 */
struct BuildingStore : StoreDirConfig
{
    BuildingStore(const std::string & storeDir)
        : StoreDirConfig{storeDir}
    {
    }

    virtual ~BuildingStore();

    virtual std::filesystem::path getRealStoreDir() const = 0;

    virtual std::filesystem::path getBuildDir() const = 0;

    virtual const LocalSettings & getLocalSettings() const = 0;

    /**
     * Make the store that recursive-Nix daemon connections talk to.
     */
    virtual ref<Store> makeRecursiveNixStore(RestrictionContext & ctx) = 0;

    std::filesystem::path toRealPath(const StorePath & storePath) const
    {
        return getRealStoreDir() / std::string(storePath.to_string());
    }
};

std::unique_ptr<BuildingStore> makeBuildingStoreFromLocalStore(LocalStore &);

struct DerivationBuilderDeleter
{
    void operator()(DerivationBuilder * builder) noexcept;
};

using DerivationBuilderUnique = std::unique_ptr<DerivationBuilder, DerivationBuilderDeleter>;

/**
 * @param ioport The worker's I/O completion port, which the log pipe is tied to.
 */
DerivationBuilderUnique makeDerivationBuilder(
    std::unique_ptr<BuildingStore> store,
    std::shared_ptr<DerivationBuilderCallbacks> miscMethods,
    DerivationBuilderParams params
#ifdef _WIN32
    ,
    HANDLE ioport
#endif
);

#ifndef _WIN32 // TODO enable `ExternalDerivationBuilder` on Windows
/**
 * @param handler Must be chosen such that it supports the given
 * derivation.
 */
DerivationBuilderUnique makeExternalDerivationBuilder(
    std::unique_ptr<BuildingStore> store,
    std::shared_ptr<DerivationBuilderCallbacks> miscMethods,
    DerivationBuilderParams params,
    const ExternalBuilder & handler);
#endif

} // namespace nix

JSON_IMPL(nix::ExternalBuilder)
