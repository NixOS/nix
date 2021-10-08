#pragma once

#include "derivation-goal.hh"
#include "local-store.hh"

namespace nix {

struct LocalDerivationGoal : public DerivationGoal
{
    LocalStore & getLocalStore();

    /* User selected for running the builder. */
    std::unique_ptr<UserLock> buildUser;

    /* The process ID of the builder. */
    Pid pid;

    /* The temporary directory. */
    Path tmpDir;

    /* The path of the temporary directory in the sandbox. */
    Path tmpDirInSandbox;

    /* Pipe for the builder's standard output/error. */
    Pipe builderOut;

    /* Pipe for synchronising updates to the builder namespaces. */
    Pipe userNamespaceSync;

    /* The mount namespace and user namespace of the builder, used to add additional
       paths to the sandbox as a result of recursive Nix calls. */
    AutoCloseFD sandboxMountNamespace;
    AutoCloseFD sandboxUserNamespace;

    /* On Linux, whether we're doing the build in its own user
       namespace. */
    bool usingUserNamespace = true;

    /* Whether we're currently doing a chroot build. */
    bool useChroot = false;

    Path chrootRootDir;

    /* RAII object to delete the chroot directory. */
    std::shared_ptr<AutoDelete> autoDelChroot;

    /* Whether to run the build in a private network namespace. */
    bool privateNetwork = false;

    /* Stuff we need to pass to initChild(). */
    struct ChrootPath {
        Path source;
        bool optional;
        ChrootPath(Path source = "", bool optional = false)
            : source(source), optional(optional)
        { }
    };
    typedef map<Path, ChrootPath> DirsInChroot; // maps target path to source path
    DirsInChroot dirsInChroot;

    typedef map<string, string> Environment;
    Environment env;

#if __APPLE__
    typedef string SandboxProfile;
    SandboxProfile additionalSandboxProfile;
#endif

    /* Hash rewriting. */
    StringMap inputRewrites, outputRewrites;
    typedef map<StorePath, StorePath> RedirectedOutputs;
    RedirectedOutputs redirectedOutputs;

    /* The outputs paths used during the build.

       - Input-addressed derivations or fixed content-addressed outputs are
         sometimes built when some of their outputs already exist, and can not
         be hidden via sandboxing. We use temporary locations instead and
         rewrite after the build. Otherwise the regular predetermined paths are
         put here.

       - Floating content-addressed derivations do not know their final build
         output paths until the outputs are hashed, so random locations are
         used, and then renamed. The randomness helps guard against hidden
         self-references.
     */
    OutputPathMap scratchOutputs;

    /* Path registration info from the previous round, if we're
       building multiple times. Since this contains the hash, it
       allows us to compare whether two rounds produced the same
       result. */
    std::map<Path, ValidPathInfo> prevInfos;

    uid_t sandboxUid() { return usingUserNamespace ? 1000 : buildUser->getUID(); }
    gid_t sandboxGid() { return usingUserNamespace ?  100 : buildUser->getGID(); }

    const static Path homeDir;

    /* The recursive Nix daemon socket. */
    AutoCloseFD daemonSocket;

    /* The daemon main thread. */
    std::thread daemonThread;

    /* The daemon worker threads. */
    std::vector<std::thread> daemonWorkerThreads;

    /* Paths that were added via recursive Nix calls. */
    StorePathSet addedPaths;

    /* Realisations that were added via recursive Nix calls. */
    std::set<DrvOutput> addedDrvOutputs;

    /* Recursive Nix calls are only allowed to build or realize paths
       in the original input closure or added via a recursive Nix call
       (so e.g. you can't do 'nix-store -r /nix/store/<bla>' where
       /nix/store/<bla> is some arbitrary path in a binary cache). */
    bool isAllowed(const StorePath & path)
    {
        return inputPaths.count(path) || addedPaths.count(path);
    }
    bool isAllowed(const DrvOutput & id)
    {
        return addedDrvOutputs.count(id);
    }

    bool isAllowed(const DerivedPath & req);

    friend struct RestrictedStore;

    using DerivationGoal::DerivationGoal;

    virtual ~LocalDerivationGoal() override;

    /* Whether we need to perform hash rewriting if there are valid output paths. */
    bool needsHashRewrite();

    /* The additional states. */
    void tryLocalBuild() override;

    /* Start building a derivation. */
    void startBuilder();

    /* Fill in the environment for the builder. */
    void initEnv();

    /* Setup tmp dir location. */
    void initTmpDir();

    /* Write a JSON file containing the derivation attributes. */
    void writeStructuredAttrs();

    void startDaemon();

    void stopDaemon();

    /* Add 'path' to the set of paths that may be referenced by the
       outputs, and make it appear in the sandbox. */
    void addDependency(const StorePath & path);

    /* Make a file owned by the builder. */
    void chownToBuilder(const Path & path);

    int getChildStatus() override;

    /* Run the builder's process. */
    void runChild();

    /* Check that the derivation outputs all exist and register them
       as valid. */
    void registerOutputs() override;

    void signRealisation(Realisation &) override;

    /* Check that an output meets the requirements specified by the
       'outputChecks' attribute (or the legacy
       '{allowed,disallowed}{References,Requisites}' attributes). */
    void checkOutputs(const std::map<std::string, ValidPathInfo> & outputs);

    /* Close the read side of the logger pipe. */
    void closeReadPipes() override;

    /* Cleanup hooks for buildDone() */
    void cleanupHookFinally() override;
    void cleanupPreChildKill() override;
    void cleanupPostChildKill() override;
    bool cleanupDecideWhetherDiskFull() override;
    void cleanupPostOutputsRegisteredModeCheck() override;
    void cleanupPostOutputsRegisteredModeNonCheck() override;

    bool isReadDesc(int fd) override;

    /* Delete the temporary directory, if we have one. */
    void deleteTmpDir(bool force);

    /* Forcibly kill the child process, if any. */
    void killChild() override;

    /* Create alternative path calculated from but distinct from the
       input, so we can avoid overwriting outputs (or other store paths)
       that already exist. */
    StorePath makeFallbackPath(const StorePath & path);
    /* Make a path to another based on the output name along with the
       derivation hash. */
    /* FIXME add option to randomize, so we can audit whether our
       rewrites caught everything */
    StorePath makeFallbackPath(std::string_view outputName);
};

}
