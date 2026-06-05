#pragma once

#include "nix/store/aws-creds.hh"
#include "nix/store/build/derivation-builder.hh"
#include "nix/store/globals.hh"
#include "nix/store/local-store.hh"
#include "nix/store/active-builds.hh"
#include "nix/store/user-lock.hh"

namespace nix {

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
// FIXME: rename this to UnixDerivationBuilder or something like that.
class DerivationBuilderImpl : public DerivationBuilder, public DerivationBuilderParams
{
private:
    void anchor() override;

protected:

    /**
     * The process ID of the builder.
     */
    Pid pid;

    /**
     * Handles to track active builds for `nix ps`.
     */
    std::optional<TrackActiveBuildsStore::BuildHandle> activeBuildHandle;

    LocalStore & store;

    const LocalSettings & localSettings = store.config->getLocalSettings();

    std::shared_ptr<DerivationBuilderCallbacks> miscMethods;

public:

    DerivationBuilderImpl(
        LocalStore & store, std::shared_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
        : DerivationBuilderParams{std::move(params)}
        , store{store}
        , miscMethods{miscMethods}
        , derivationType{drv.type()}
    {
    }

    /**
     * Cleanup code to run when destroying any DerivationBuilderImpl implementation.
     */
    void cleanupOnDestruction() noexcept
    {
        /* Careful: we should never ever throw an exception from a
           noexcept function. */
        try {
            killChild();
        } catch (...) {
            ignoreExceptionInDestructor();
        }
        try {
            stopDaemon();
        } catch (...) {
            ignoreExceptionInDestructor();
        }
        try {
            cleanupBuild(false);
        } catch (...) {
            ignoreExceptionInDestructor();
        }
    }

protected:

    /**
     * User selected for running the builder.
     */
    std::unique_ptr<UserLock> buildUser;

    /**
     * The temporary directory used for the build.
     */
    std::filesystem::path tmpDir;

    /**
     * The top-level temporary directory. `tmpDir` is either equal to
     * or a child of this directory.
     */
    std::filesystem::path topTmpDir;

    /**
     * The file descriptor of the temporary directory.
     */
    AutoCloseFD tmpDirFd;

    /**
     * The sort of derivation we are building.
     *
     * Just a cached value, computed from `drv`.
     */
    const DerivationType derivationType;

    typedef StringMap Environment;
    Environment env;

    /**
     * Hash rewriting.
     */
    StringMap inputRewrites, outputRewrites;
    typedef std::map<StorePath, StorePath> RedirectedOutputs;
    RedirectedOutputs redirectedOutputs;

    /**
     * The output paths used during the build.
     *
     * - Input-addressed derivations or fixed content-addressed outputs are
     *   sometimes built when some of their outputs already exist, and can not
     *   be hidden via sandboxing. We use temporary locations instead and
     *   rewrite after the build. Otherwise the regular predetermined paths are
     *   put here.
     *
     * - Floating content-addressing derivations do not know their final build
     *   output paths until the outputs are hashed, so random locations are
     *   used, and then renamed. The randomness helps guard against hidden
     *   self-references.
     */
    OutputPathMap scratchOutputs;

    const static std::filesystem::path homeDir;

    /**
     * The recursive Nix daemon socket.
     */
    AutoCloseFD daemonSocket;

    /**
     * The daemon main thread.
     */
    std::thread daemonThread;

    struct DaemonWorkerState
    {
        std::thread thread;
        ref<std::atomic_flag> done;
    };

    /**
     * The daemon worker threads.
     */
    std::list<DaemonWorkerState> daemonWorkerThreads;

    const StorePathSet & originalPaths() override
    {
        return inputPaths;
    }

    bool isAllowed(const StorePath & path) override
    {
        if (inputPaths.count(path))
            return true;
        auto state(state_.lock());
        auto iter = state->addedPaths.find(path);
        if (iter == state->addedPaths.end())
            return false;
        return iter->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    bool isAllowed(const DrvOutput & id) override
    {
        return state_.lock()->addedDrvOutputs.count(id);
    }

    bool isAllowed(const DerivedPath & req);

    friend struct RestrictedStore;

    /**
     * Whether we need to perform hash rewriting if there are valid output paths.
     */
    virtual bool needsHashRewrite()
    {
        return true;
    }

public:

    std::optional<Descriptor> startBuild() override;

    SingleDrvOutputs unprepareBuild() override;

protected:

    /**
     * Acquire a build user lock. Return nullptr if no lock is available.
     */
    virtual std::unique_ptr<UserLock> getBuildUser()
    {
        return acquireUserLock(settings.nixStateDir, localSettings, 1, false);
    }

    /**
     * Construct the `ActiveBuild` object for `ActiveBuildsTracker`.
     */
    virtual ActiveBuild getActiveBuild();

    /**
     * Return the paths that should be made available in the sandbox.
     * This includes:
     *
     * * The paths specified by the `sandbox-paths` setting, and their closure in the Nix store.
     * * The contents of the `__impureHostDeps` derivation attribute, if the sandbox is in relaxed mode.
     * * The paths returned by the `pre-build-hook`.
     * * The paths in the input closure of the derivation.
     */
    PathsInChroot getPathsInSandbox();

    virtual void setBuildTmpDir()
    {
        tmpDir = topTmpDir;
    }

    /**
     * Return the path of the temporary directory in the sandbox.
     */
    virtual std::filesystem::path tmpDirInSandbox()
    {
        assert(!topTmpDir.empty());
        return topTmpDir;
    }

    /**
     * Ensure that there are no processes running that conflict with
     * `buildUser`.
     */
    virtual void prepareUser()
    {
        killSandbox(false);
    }

    /**
     * Called by prepareBuild() to do any setup in the parent to
     * prepare for a sandboxed build.
     */
    virtual void prepareSandbox();

    virtual Strings getPreBuildHookArgs()
    {
        return Strings({store.printStorePath(drvPath)});
    }

    virtual std::filesystem::path realPathInHost(const std::filesystem::path & p)
    {
        return store.toRealPath(store.parseStorePath(p.native()));
    }

    /**
     * Open the slave side of the pseudoterminal and use it as stderr.
     */
    void openSlave();

    /**
     * Called by prepareBuild() to start the child process for the
     * build. Must set `pid`. The child must call runChild().
     */
    virtual void startChild();

#if NIX_WITH_AWS_AUTH
    /**
     * Pre-resolve AWS credentials for S3 URLs in builtin:fetchurl.
     * This should be called before forking to ensure credentials are available in child.
     * Returns the credentials if successfully resolved, or std::nullopt otherwise.
     */
    std::optional<AwsCredentials> preResolveAwsCredentials();
#endif

private:

    /**
     * Fill in the environment for the builder.
     */
    void initEnv();

protected:

    /**
     * Process messages send by the sandbox initialization.
     */
    void processSandboxSetupMessages();

private:

    /**
     * Start an in-process nix daemon thread for recursive-nix.
     */
    void startDaemon();

    /**
     * Stop the in-process nix daemon thread.
     * @see startDaemon
     */
    void stopDaemon();

protected:

    void addDependencyImpl(const StorePath & path) override;

    /**
     * Make a file owned by the builder.
     *
     * SAFETY: this function is prone to TOCTOU as it receives a path and not a descriptor.
     * It's only safe to call in a child of a directory only visible to the owner.
     */
    void chownToBuilder(const std::filesystem::path & path);

    /**
     * Make a file owned by the builder addressed by its file descriptor.
     *
     * @param path Only used for error messages.
     */
    void chownToBuilder(int fd, const std::filesystem::path & path);

    /**
     * Create a file in `tmpDir` owned by the builder.
     *
     * @param Name must not contain more than one path segment and none of them must be `..`, `.`
     * Otherwise this function throws an Error.
     */
    void writeBuilderFile(const std::string & name, std::string_view contents);

    /**
     * Arguments passed to runChild().
     */
    struct RunChildArgs
    {
#if NIX_WITH_AWS_AUTH
        std::optional<AwsCredentials> awsCredentials;
#endif
    };

    /**
     * Run the builder's process.
     */
    void runChild(RunChildArgs args);

    /**
     * Move the current process into the chroot, if any. Called early
     * by runChild().
     */
    virtual void enterChroot() {}

    /**
     * Change the current process's uid/gid to the build user, if
     * any. Called by runChild().
     */
    virtual void setUser();

    /**
     * Execute the derivation builder process. Called by runChild() as
     * its final step. Should not return unless there is an error.
     */
    virtual void execBuilder(const Strings & args, const Strings & envStrs);

private:

    /**
     * Check that the derivation outputs all exist and register them
     * as valid.
     */
    SingleDrvOutputs registerOutputs();

protected:

    /**
     * Delete the temporary directory, if we have one.
     *
     * @param force We know the build succeeded, so don't attempt to
     * preserve anything for debugging.
     */
    virtual void cleanupBuild(bool force);

    /**
     * Kill any processes running under the build user UID or in the
     * cgroup of the build.
     */
    virtual void killSandbox(bool getStats);

public:

    bool killChild() override;

private:

    bool decideWhetherDiskFull();

    /**
     * Create alternative path calculated from but distinct from the
     * input, so we can avoid overwriting outputs (or other store paths)
     * that already exist.
     */
    StorePath makeFallbackPath(const StorePath & path);

    /**
     * Make a path to another based on the output name along with the
     * derivation hash.
     *
     * @todo Add option to randomize, so we can audit whether our
     * rewrites caught everything
     */
    StorePath makeFallbackPath(OutputNameView outputName);
};

} // namespace nix
