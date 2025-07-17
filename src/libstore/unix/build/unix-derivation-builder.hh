#pragma once
///@file

#include "nix/store/build/derivation-builder.hh"
#include "nix/util/file-system.hh"
#include "nix/store/local-store.hh"
#include "nix/store/user-lock.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/store/restricted-store.hh"
#include "nix/util/processes.hh"

namespace nix {

/**
 * Common types and definitions for Unix derivation builders
 */

/**
 * Stuff we need to pass to initChild().
 */
struct ChrootPath
{
    Path source;
    bool optional;
    ChrootPath(Path source = "", bool optional = false)
        : source(source)
        , optional(optional)
    {
    }
};

typedef std::map<Path, ChrootPath> PathsInChroot; // maps target path to source path

/**
 * Handle the current exception by formatting an error and optionally
 * sending it to the parent process.
 */
void handleChildException(bool sendException);

/**
 * Base implementation class for Unix derivation builders.
 * This class is used by both Linux and Darwin builders.
 */
class DerivationBuilderImpl : public DerivationBuilder, public DerivationBuilderParams
{
protected:

    Store & store;

    std::unique_ptr<DerivationBuilderCallbacks> miscMethods;

public:

    DerivationBuilderImpl(
        Store & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params);

protected:

    /**
     * User selected for running the builder.
     */
    std::unique_ptr<UserLock> buildUser;

    /**
     * The temporary directory used for the build.
     */
    Path tmpDir;

    /**
     * The top-level temporary directory. `tmpDir` is either equal to
     * or a child of this directory.
     */
    Path topTmpDir;

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

    const static Path homeDir;

    /**
     * The recursive Nix daemon socket.
     */
    AutoCloseFD daemonSocket;

    /**
     * The daemon main thread.
     */
    std::thread daemonThread;

    /**
     * The daemon worker threads.
     */
    std::vector<std::thread> daemonWorkerThreads;

    const StorePathSet & originalPaths() override;

    bool isAllowed(const StorePath & path) override;
    bool isAllowed(const DrvOutput & id) override;

    friend struct RestrictedStore;

    /**
     * Whether we need to perform hash rewriting if there are valid output paths.
     */
    virtual bool needsHashRewrite();

public:

    bool prepareBuild() override;

    void startBuilder() override;

    std::variant<std::pair<BuildResult::Status, Error>, SingleDrvOutputs> unprepareBuild() override;

protected:

    /**
     * Acquire a build user lock. Return nullptr if no lock is available.
     */
    virtual std::unique_ptr<UserLock> getBuildUser();

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

    virtual void setBuildTmpDir();

    /**
     * Return the path of the temporary directory in the sandbox.
     */
    virtual Path tmpDirInSandbox();

    /**
     * Ensure that there are no processes running that conflict with
     * `buildUser`.
     */
    virtual void prepareUser();

    /**
     * Called by prepareBuild() to do any setup in the parent to
     * prepare for a sandboxed build.
     */
    virtual void prepareSandbox();

    virtual Strings getPreBuildHookArgs();

    virtual Path realPathInSandbox(const Path & p);

    /**
     * Open the slave side of the pseudoterminal and use it as stderr.
     */
    void openSlave();

    /**
     * Called by prepareBuild() to start the child process for the
     * build. Must set `pid`. The child must call runChild().
     */
    virtual void startChild();

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

    /**
     * Handle the current exception by formatting an error and optionally
     * sending it to the parent process.
     */
    void handleChildException(bool sendException);

private:

    /**
     * Write a JSON file containing the derivation attributes.
     */
    void writeStructuredAttrs();

    /**
     * Start an in-process nix daemon thread for recursive-nix.
     */
    void startDaemon();

public:

    void stopDaemon() override;

private:

    void addDependency(const StorePath & path) override;

protected:

    /**
     * Make a file owned by the builder.
     *
     * SAFETY: this function is prone to TOCTOU as it receives a path and not a descriptor.
     * It's only safe to call in a child of a directory only visible to the owner.
     */
    void chownToBuilder(const Path & path);

    /**
     * Make a file owned by the builder addressed by its file descriptor.
     */
    void chownToBuilder(int fd, const Path & path);

    /**
     * Create a file in `tmpDir` owned by the builder.
     */
    void writeBuilderFile(const std::string & name, std::string_view contents);

    /**
     * Run the builder's process.
     */
    void runChild();

    /**
     * Move the current process into the chroot, if any. Called early
     * by runChild().
     */
    virtual void enterChroot();

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

    /**
     * Check that an output meets the requirements specified by the
     * 'outputChecks' attribute (or the legacy
     * '{allowed,disallowed}{References,Requisites}' attributes).
     */
    void checkOutputs(const std::map<std::string, ValidPathInfo> & outputs);

public:

    void deleteTmpDir(bool force) override;

    void killSandbox(bool getStats) override;

protected:

    virtual void cleanupBuild();

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

}