#pragma once
///@file

#include "nix/store/derivations.hh"
#include "nix/store/local-store.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/store/derivation-options.hh"
#include "nix/store/build/derivation-building-misc.hh"
#include "nix/store/store-api.hh"
#include "nix/store/pathlocks.hh"
#include "nix/store/build/goal.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/build/build-log.hh"

namespace nix {

using std::map;

struct BuilderFailureError;
struct ExternalBuilder;
#ifndef _WIN32 // TODO enable build hook on Windows
struct DerivationBuilder;
#endif

/**
 * A goal for building a derivation. Substitution, (or any other method of
 * obtaining the outputs) will not be attempted, so it is the calling goal's
 * responsibility to try to substitute first.
 */
struct DerivationBuildingGoal : public Goal
{
    friend class Worker;

    /**
     * @param drv The derivation to build, with the outputs of its input
     * derivations already added to its input sources.
     */
    DerivationBuildingGoal(
        const StorePath & drvPath, ref<const BasicDerivation> drv, Worker & worker, BuildMode buildMode);
    ~DerivationBuildingGoal();

private:

    /** The path of the derivation. */
    const StorePath drvPath;

    /**
     * The derivation to build.
     */
    const ref<const BasicDerivation> drv;

    /**
     * The remainder is state held during the build.
     */

    const BuildMode buildMode;

    std::unique_ptr<MaintainCount<uint64_t>> mcRunningBuilds;

    std::string key() override;

    struct LocalBuildCapability
    {
        LocalStore & localStore;
        const ExternalBuilder * externalBuilder;
    };

    /**
     * The states.
     */
    using Result = decltype(BuildResult::inner);

    struct NeedsSlot
    {};

    using LocalBuildOutcome = std::variant<Result, NeedsSlot>;

    asio::awaitable<ExitCode> init();
    asio::awaitable<Result> tryToBuild();
    /**
     * Build on a builder from `builders` (what the build hook used to
     * do): copy the inputs over, build there, copy the outputs back.
     * Returns nothing if the builder turned out to be unusable, so that
     * the caller can pick another one.
     */
    asio::awaitable<std::optional<Result>> buildRemotely(
        Machine & machine,
        AutoCloseFD slotLock,
        StorePathSet inputPaths,
        std::map<std::string, InitialOutput> initialOutputs,
        DerivationOptions<StorePath> drvOptions,
        PathLocks outputLocks);

    /**
     * Copy the outputs of a build done in another store into ours, and
     * register their realisations.
     */
    asio::awaitable<void>
    copyOutputsFromBuilder(Store & builderStore, std::string_view builderName, const SingleDrvOutputs & outputs);
    asio::awaitable<LocalBuildOutcome> buildLocally(
        LocalBuildCapability localBuildCap,
        const StorePathSet & inputPaths,
        std::map<std::string, InitialOutput> & initialOutputs,
        const DerivationOptions<StorePath> & drvOptions,
        PathLocks outputLocks,
        /* Null when no local build slot is needed (building in another store). */
        std::optional<AsyncSemaphore::Handle> * buildSlot,
        /* Name of the builder, if not building in our own store. */
        std::optional<std::string> builderName);

    BuildError logLimitExceeded();

    /**
     * Wrappers around the corresponding Store methods that first consult the
     * derivation.  This is currently needed because when there is no drv file
     * there also is no DB entry.
     */
    std::map<std::string, std::optional<StorePath>> queryPartialDerivationOutputMap();

    /**
     * Update 'initialOutputs' to determine the current status of the
     * outputs of the derivation. Also returns a Boolean denoting
     * whether all outputs are valid and non-corrupt, and a
     * 'SingleDrvOutputs' structure containing the valid outputs.
     */
    std::pair<bool, SingleDrvOutputs> checkPathValidity(std::map<std::string, InitialOutput> & initialOutputs);

    BuildError fixupBuilderFailureErrorMessage(BuilderFailureError msg, BuildLog & buildLog);

    JobCategory jobCategory() const override
    {
        return JobCategory::Build;
    };
};

} // namespace nix
