#pragma once
///@file

#include <nlohmann/json_fwd.hpp>

#include "nix/store/build-result.hh"
#include "nix/store/derivation-options.hh"
#include "nix/store/build/derivation-building-misc.hh"
#include "nix/store/derivations.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/util/processes.hh"
#include "nix/util/json-impls.hh"
#include "nix/store/restricted-store.hh"
#include "nix/store/build/derivation-env-desugar.hh"

namespace nix {

/**
 * Denotes a build failure that stemmed from the builder exiting with a
 * failing exist status.
 */
struct BuilderFailureError : BuildError
{
    int builderStatus;

    std::string extraMsgAfter;

    BuilderFailureError(BuildResult::Failure::Status status, int builderStatus, std::string extraMsgAfter)
        : BuildError{
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
    Path source;
    bool optional = false;
};

typedef std::map<Path, ChrootPath> PathsInChroot; // maps target path to source path

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
    const DerivationOptions & drvOptions;

    // The remainder is state held during the build.

    /**
     * All input paths (that is, the union of FS closures of the
     * immediate input paths).
     */
    const StorePathSet & inputPaths;

    const std::map<std::string, InitialOutput> & initialOutputs;

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
    virtual ~DerivationBuilderCallbacks() = default;

    /**
     * Open a log file and a pipe to it.
     */
    virtual Path openLogFile() = 0;

    /**
     * Close the log file.
     */
    virtual void closeLogFile() = 0;

    /**
     * @todo this should be reworked
     */
    virtual void childTerminated() = 0;
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
    DerivationBuilder() = default;
    virtual ~DerivationBuilder() = default;

    /**
     * Master side of the pseudoterminal used for the builder's
     * standard output/error.
     */
    AutoCloseFD builderOut;

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
     * @returns The first case indicates failure during output
     * processing. A status code and exception are returned, providing
     * more information. The second case indicates success, and
     * realisations for each output of the derivation are returned.
     *
     * @throws BuildError
     */
    virtual SingleDrvOutputs unprepareBuild() = 0;

    /**
     * Forcibly kill the child process, if any.
     *
     * @returns whether the child was still alive and needed to be
     * killed.
     */
    virtual bool killChild() = 0;
};

struct ExternalBuilder
{
    StringSet systems;
    Path program;
    std::vector<std::string> args;
};

#ifndef _WIN32 // TODO enable `DerivationBuilder` on Windows
std::unique_ptr<DerivationBuilder> makeDerivationBuilder(
    LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params);

/**
 * @param handler Must be chosen such that it supports the given
 * derivation.
 */
std::unique_ptr<DerivationBuilder> makeExternalDerivationBuilder(
    LocalStore & store,
    std::unique_ptr<DerivationBuilderCallbacks> miscMethods,
    DerivationBuilderParams params,
    const ExternalBuilder & handler);
#endif

} // namespace nix

JSON_IMPL(nix::ExternalBuilder)
