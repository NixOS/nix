#pragma once
///@file

#include "nix/store/build-result.hh"
#include "nix/store/derivation-options.hh"
#include "nix/store/build/derivation-building-misc.hh"
#include "nix/store/derivations.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/util/processes.hh"
#include "nix/store/restricted-store.hh"

namespace nix {

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
    const Derivation & drv;

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

    /**
     * @note we do in fact mutate this
     */
    std::map<std::string, InitialOutput> & initialOutputs;

    const BuildMode & buildMode;

    /**
     * Extra paths we want to be in the chroot, regardless of the
     * derivation we are building.
     */
    PathsInChroot defaultPathsInChroot;

    struct EnvEntry
    {
        /**
         * Actually, this should be passed as a file, but with a custom
         * name (rather than hash-derived name for usual "pass as file").
         */
        std::optional<std::string> nameOfPassAsFile;

        /**
         * String value of env var, or contents of the file
         */
        std::string value;
    };

    /**
     * The final environment variables to additionally set, possibly
     * indirectly via a file.
     *
     * This is used by the caller to desugar the "structured attrs"
     * mechanism, so `DerivationBuilder` doesn't need to know about it.
     */
    std::map<std::string, EnvEntry, std::less<>> finalEnv;

    /**
     * Inserted in the temp dir, but no file names placed in env, unlike
     * `EnvEntry::nameOfPassAsFile` above.
     */
    StringMap extraFiles;

    DerivationBuilderParams(
        const StorePath & drvPath,
        const BuildMode & buildMode,
        BuildResult & buildResult,
        const Derivation & drv,
        const DerivationOptions & drvOptions,
        const StorePathSet & inputPaths,
        std::map<std::string, InitialOutput> & initialOutputs,
        PathsInChroot defaultPathsInChroot,
        std::map<std::string, EnvEntry, std::less<>> finalEnv,
        StringMap extraFiles)
        : drvPath{drvPath}
        , buildResult{buildResult}
        , drv{drv}
        , drvOptions{drvOptions}
        , inputPaths{inputPaths}
        , initialOutputs{initialOutputs}
        , buildMode{buildMode}
        , defaultPathsInChroot{std::move(defaultPathsInChroot)}
        , finalEnv{std::move(finalEnv)}
        , extraFiles{std::move(extraFiles)}
    {
    }

    DerivationBuilderParams(DerivationBuilderParams &&) = default;
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

    virtual void appendLogTailErrorMsg(std::string & msg) = 0;

    /**
     * Hook up `builderOut` to some mechanism to ingest the log
     *
     * @todo this should be reworked
     */
    virtual void childStarted(Descriptor builderOut) = 0;

    /**
     * @todo this should be reworked
     */
    virtual void childTerminated() = 0;

    virtual void noteHashMismatch(void) = 0;
    virtual void noteCheckMismatch(void) = 0;

    virtual void markContentsGood(const StorePath & path) = 0;
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
    /**
     * The process ID of the builder.
     */
    Pid pid;

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
     * @returns true if successful, false if we could not acquire a build
     * user. In that case, the caller must wait and then try again.
     */
    virtual bool prepareBuild() = 0;

    /**
     * Start building a derivation.
     */
    virtual void startBuilder() = 0;

    /**
     * Tear down build environment after the builder exits (either on
     * its own or if it is killed).
     *
     * @returns The first case indicates failure during output
     * processing. A status code and exception are returned, providing
     * more information. The second case indicates success, and
     * realisations for each output of the derivation are returned.
     */
    virtual std::variant<std::pair<BuildResult::Status, Error>, SingleDrvOutputs> unprepareBuild() = 0;

    /**
     * Stop the in-process nix daemon thread.
     * @see startDaemon
     */
    virtual void stopDaemon() = 0;

    /**
     * Delete the temporary directory, if we have one.
     */
    virtual void deleteTmpDir(bool force) = 0;

    /**
     * Kill any processes running under the build user UID or in the
     * cgroup of the build.
     */
    virtual void killSandbox(bool getStats) = 0;
};

#ifndef _WIN32 // TODO enable `DerivationBuilder` on Windows
std::unique_ptr<DerivationBuilder> makeDerivationBuilder(
    LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params);
#endif

} // namespace nix
