#include "nix/store/build/derivation-builder.hh"
#include "nix/util/file-system.hh"
#include "nix/store/local-store.hh"
#include "nix/util/processes.hh"
#include "nix/store/builtins.hh"
#include "nix/store/path-references.hh"
#include "nix/util/finally.hh"
#include "nix/util/util.hh"
#include "nix/util/archive.hh"
#include "nix/util/git.hh"
#include "nix/store/daemon.hh"
#include "nix/util/topo-sort.hh"
#include "nix/store/build/child.hh"
#include "nix/util/unix-domain-socket.hh"
#include "nix/store/posix-fs-canonicalise.hh"
#include "nix/util/posix-source-accessor.hh"
#include "nix/store/restricted-store.hh"
#include "nix/store/user-lock.hh"
#include "nix/store/globals.hh"
#include "nix/store/build/derivation-env-desugar.hh"
#include "nix/util/terminal.hh"

#include <queue>

#include <sys/un.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>

#include "store-config-private.hh"

#if HAVE_STATVFS
#  include <sys/statvfs.h>
#endif

#include <pwd.h>
#include <grp.h>
#include <iostream>

#include "nix/util/strings.hh"
#include "nix/util/signals.hh"

#include "store-config-private.hh"
#include "build/derivation-check.hh"

#if NIX_WITH_AWS_AUTH
#  include "nix/store/aws-creds.hh"
#  include "nix/store/s3-url.hh"
#  include "nix/util/url.hh"
#endif

namespace nix {

struct NotDeterministic : BuildError
{
    NotDeterministic(auto &&... args)
        : BuildError(BuildResult::Failure::NotDeterministic, args...)
    {
    }
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
// FIXME: rename this to UnixDerivationBuilder or something like that.
class DerivationBuilderImpl : public DerivationBuilder, public DerivationBuilderParams
{
protected:

    /**
     * The process ID of the builder.
     */
    Pid pid;

    LocalStore & store;

    std::unique_ptr<DerivationBuilderCallbacks> miscMethods;

public:

    DerivationBuilderImpl(
        LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
        : DerivationBuilderParams{std::move(params)}
        , store{store}
        , miscMethods{std::move(miscMethods)}
        , derivationType{drv.type()}
    {
    }

    ~DerivationBuilderImpl()
    {
        /* Careful: we should never ever throw an exception from a
           destructor. */
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

    const StorePathSet & originalPaths() override
    {
        return inputPaths;
    }

    bool isAllowed(const StorePath & path) override
    {
        return inputPaths.count(path) || addedPaths.count(path);
    }

    bool isAllowed(const DrvOutput & id) override
    {
        return addedDrvOutputs.count(id);
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
        return acquireUserLock(1, false);
    }

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
    virtual Path tmpDirInSandbox()
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

    virtual Path realPathInSandbox(const Path & p)
    {
        return store.toRealPath(p);
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
     * @param force We know the build suceeded, so don't attempt to
     * preseve anything for debugging.
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

void handleDiffHook(
    uid_t uid, uid_t gid, const Path & tryA, const Path & tryB, const Path & drvPath, const Path & tmpDir)
{
    auto & diffHookOpt = settings.diffHook.get();
    if (diffHookOpt && settings.runDiffHook) {
        auto & diffHook = *diffHookOpt;
        try {
            auto diffRes = runProgram(
                RunOptions{
                    .program = diffHook,
                    .lookupPath = true,
                    .args = {tryA, tryB, drvPath, tmpDir},
                    .uid = uid,
                    .gid = gid,
                    .chdir = "/"});
            if (!statusOk(diffRes.first))
                throw ExecError(diffRes.first, "diff-hook program '%1%' %2%", diffHook, statusToString(diffRes.first));

            if (diffRes.second != "")
                printError(chomp(diffRes.second));
        } catch (Error & error) {
            ErrorInfo ei = error.info();
            // FIXME: wrap errors.
            ei.msg = HintFmt("diff hook execution failed: %s", ei.msg.str());
            logError(ei);
        }
    }
}

const Path DerivationBuilderImpl::homeDir = "/homeless-shelter";

void DerivationBuilderImpl::killSandbox(bool getStats)
{
    if (buildUser) {
        auto uid = buildUser->getUID();
        assert(uid != 0);
        killUser(uid);
    }
}

bool DerivationBuilderImpl::killChild()
{
    bool ret = pid != -1;
    if (ret) {
        /* If we're using a build user, then there is a tricky race
           condition: if we kill the build user before the child has
           done its setuid() to the build user uid, then it won't be
           killed, and we'll potentially lock up in pid.wait().  So
           also send a conventional kill to the child. */
        ::kill(-pid, SIGKILL); /* ignore the result */

        killSandbox(true);

        pid.wait();
    }
    return ret;
}

SingleDrvOutputs DerivationBuilderImpl::unprepareBuild()
{
    /* Since we got an EOF on the logger pipe, the builder is presumed
       to have terminated.  In fact, the builder could also have
       simply have closed its end of the pipe, so just to be sure,
       kill it. */
    int status = pid.kill();

    debug("builder process for '%s' finished", store.printStorePath(drvPath));

    buildResult.timesBuilt++;
    buildResult.stopTime = time(0);

    /* So the child is gone now. */
    miscMethods->childTerminated();

    /* Close the read side of the logger pipe. */
    builderOut.close();

    /* Close the log file. */
    miscMethods->closeLogFile();

    /* When running under a build user, make sure that all processes
       running under that uid are gone.  This is to prevent a
       malicious user from leaving behind a process that keeps files
       open and modifies them after they have been chown'ed to
       root. */
    killSandbox(true);

    /* Terminate the recursive Nix daemon. */
    stopDaemon();

    if (buildResult.cpuUser && buildResult.cpuSystem) {
        debug(
            "builder for '%s' terminated with status %d, user CPU %.3fs, system CPU %.3fs",
            store.printStorePath(drvPath),
            status,
            ((double) buildResult.cpuUser->count()) / 1000000,
            ((double) buildResult.cpuSystem->count()) / 1000000);
    }

    /* Check the exit status. */
    if (!statusOk(status)) {

        /* Check *before* cleaning up. */
        bool diskFull = decideWhetherDiskFull();

        cleanupBuild(false);

        throw BuilderFailureError{
            !derivationType.isSandboxed() || diskFull ? BuildResult::Failure::TransientFailure
                                                      : BuildResult::Failure::PermanentFailure,
            status,
            diskFull ? "\nnote: build failure may have been caused by lack of free disk space" : "",
        };
    }

    /* Compute the FS closure of the outputs and register them as
       being valid. */
    auto builtOutputs = registerOutputs();

    cleanupBuild(true);

    return builtOutputs;
}

static void chmod_(const Path & path, mode_t mode)
{
    if (chmod(path.c_str(), mode) == -1)
        throw SysError("setting permissions on '%s'", path);
}

/* Move/rename path 'src' to 'dst'. Temporarily make 'src' writable if
   it's a directory and we're not root (to be able to update the
   directory's parent link ".."). */
static void movePath(const Path & src, const Path & dst)
{
    auto st = lstat(src);

    bool changePerm = (geteuid() && S_ISDIR(st.st_mode) && !(st.st_mode & S_IWUSR));

    if (changePerm)
        chmod_(src, st.st_mode | S_IWUSR);

    std::filesystem::rename(src, dst);

    if (changePerm)
        chmod_(dst, st.st_mode);
}

static void replaceValidPath(const Path & storePath, const Path & tmpPath)
{
    /* We can't atomically replace storePath (the original) with
       tmpPath (the replacement), so we have to move it out of the
       way first.  We'd better not be interrupted here, because if
       we're repairing (say) Glibc, we end up with a broken system. */
    Path oldPath;

    if (pathExists(storePath)) {
        // why do we loop here?
        // although makeTempPath should be unique, we can't
        // guarantee that.
        do {
            oldPath = makeTempPath(storePath, ".old");
            // store paths are often directories so we can't just unlink() it
            // let's make sure the path doesn't exist before we try to use it
        } while (pathExists(oldPath));
        movePath(storePath, oldPath);
    }
    try {
        movePath(tmpPath, storePath);
    } catch (...) {
        try {
            // attempt to recover
            if (!oldPath.empty())
                movePath(oldPath, storePath);
        } catch (...) {
            ignoreExceptionExceptInterrupt();
        }
        throw;
    }
    if (!oldPath.empty())
        deletePath(oldPath);
}

bool DerivationBuilderImpl::decideWhetherDiskFull()
{
    bool diskFull = false;

    /* Heuristically check whether the build failure may have
       been caused by a disk full condition.  We have no way
       of knowing whether the build actually got an ENOSPC.
       So instead, check if the disk is (nearly) full now.  If
       so, we don't mark this build as a permanent failure. */
#if HAVE_STATVFS
    {
        uint64_t required = 8ULL * 1024 * 1024; // FIXME: make configurable
        struct statvfs st;
        if (statvfs(store.config->realStoreDir.get().c_str(), &st) == 0
            && (uint64_t) st.f_bavail * st.f_bsize < required)
            diskFull = true;
        if (statvfs(tmpDir.c_str(), &st) == 0 && (uint64_t) st.f_bavail * st.f_bsize < required)
            diskFull = true;
    }
#endif

    return diskFull;
}

/**
 * Rethrow the current exception as a subclass of `Error`.
 */
static void rethrowExceptionAsError()
{
    try {
        throw;
    } catch (Error &) {
        throw;
    } catch (std::exception & e) {
        throw Error(e.what());
    } catch (...) {
        throw Error("unknown exception");
    }
}

/**
 * Send the current exception to the parent in the format expected by
 * `DerivationBuilderImpl::processSandboxSetupMessages()`.
 */
static void handleChildException(bool sendException)
{
    try {
        rethrowExceptionAsError();
    } catch (Error & e) {
        if (sendException) {
            writeFull(STDERR_FILENO, "\1\n");
            FdSink sink(STDERR_FILENO);
            sink << e;
            sink.flush();
        } else
            std::cerr << e.msg();
    }
}

static bool checkNotWorldWritable(std::filesystem::path path)
{
    while (true) {
        auto st = lstat(path);
        if (st.st_mode & S_IWOTH)
            return false;
        if (path == path.parent_path())
            break;
        path = path.parent_path();
    }
    return true;
}

std::optional<Descriptor> DerivationBuilderImpl::startBuild()
{
    if (useBuildUsers()) {
        if (!buildUser)
            buildUser = getBuildUser();

        if (!buildUser)
            return std::nullopt;
    }

    /* Make sure that no other processes are executing under the
       sandbox uids. This must be done before any chownToBuilder()
       calls. */
    prepareUser();

    auto buildDir = store.config->getBuildDir();

    createDirs(buildDir);

    if (buildUser && !checkNotWorldWritable(buildDir))
        throw Error(
            "Path %s or a parent directory is world-writable or a symlink. That's not allowed for security.", buildDir);

    /* Create a temporary directory where the build will take
       place. */
    topTmpDir = createTempDir(buildDir, "nix", 0700);
    setBuildTmpDir();
    assert(!tmpDir.empty());

    /* The TOCTOU between the previous mkdir call and this open call is unavoidable due to
       POSIX semantics.*/
    tmpDirFd = AutoCloseFD{open(tmpDir.c_str(), O_RDONLY | O_NOFOLLOW | O_DIRECTORY)};
    if (!tmpDirFd)
        throw SysError("failed to open the build temporary directory descriptor '%1%'", tmpDir);

    chownToBuilder(tmpDirFd.get(), tmpDir);

    for (auto & [outputName, status] : initialOutputs) {
        /* Set scratch path we'll actually use during the build.

           If we're not doing a chroot build, but we have some valid
           output paths.  Since we can't just overwrite or delete
           them, we have to do hash rewriting: i.e. in the
           environment/arguments passed to the build, we replace the
           hashes of the valid outputs with unique dummy strings;
           after the build, we discard the redirected outputs
           corresponding to the valid outputs, and rewrite the
           contents of the new outputs to replace the dummy strings
           with the actual hashes. */
        auto scratchPath = !status.known ? makeFallbackPath(outputName)
                           : !needsHashRewrite()
                               /* Can always use original path in sandbox */
                               ? status.known->path
                               : !status.known->isPresent()
                                     /* If path doesn't yet exist can just use it */
                                     ? status.known->path
                                     : buildMode != bmRepair && !status.known->isValid()
                                           /* If we aren't repairing we'll delete a corrupted path, so we
                                              can use original path */
                                           ? status.known->path
                                           : /* If we are repairing or the path is totally valid, we'll need
                                                to use a temporary path */
                                           makeFallbackPath(status.known->path);
        scratchOutputs.insert_or_assign(outputName, scratchPath);

        /* Substitute output placeholders with the scratch output paths.
           We'll use during the build. */
        inputRewrites[hashPlaceholder(outputName)] = store.printStorePath(scratchPath);

        /* Additional tasks if we know the final path a priori. */
        if (!status.known)
            continue;
        auto fixedFinalPath = status.known->path;

        /* Additional tasks if the final and scratch are both known and
           differ. */
        if (fixedFinalPath == scratchPath)
            continue;

        /* Ensure scratch path is ours to use. */
        deletePath(store.printStorePath(scratchPath));

        /* Rewrite and unrewrite paths */
        {
            std::string h1{fixedFinalPath.hashPart()};
            std::string h2{scratchPath.hashPart()};
            inputRewrites[h1] = h2;
        }

        redirectedOutputs.insert_or_assign(std::move(fixedFinalPath), std::move(scratchPath));
    }

    /* Construct the environment passed to the builder. */
    initEnv();

    prepareSandbox();

    if (needsHashRewrite() && pathExists(homeDir))
        throw Error(
            "home directory '%1%' exists; please remove it to assure purity of builds without sandboxing", homeDir);

    /* Fire up a Nix daemon to process recursive Nix calls from the
       builder. */
    if (drvOptions.getRequiredSystemFeatures(drv).count("recursive-nix"))
        startDaemon();

    /* Run the builder. */
    printMsg(lvlChatty, "executing builder '%1%'", drv.builder);
    printMsg(lvlChatty, "using builder args '%1%'", concatStringsSep(" ", drv.args));
    for (auto & i : drv.env)
        printMsg(lvlVomit, "setting builder env variable '%1%'='%2%'", i.first, i.second);

    /* Create the log file. */
    miscMethods->openLogFile();

    /* Create a pseudoterminal to get the output of the builder. */
    builderOut = posix_openpt(O_RDWR | O_NOCTTY);
    if (!builderOut)
        throw SysError("opening pseudoterminal master");

    std::string slaveName = getPtsName(builderOut.get());

    if (buildUser) {
        if (chmod(slaveName.c_str(), 0600))
            throw SysError("changing mode of pseudoterminal slave");

        if (chown(slaveName.c_str(), buildUser->getUID(), 0))
            throw SysError("changing owner of pseudoterminal slave");
    }
#ifdef __APPLE__
    else {
        if (grantpt(builderOut.get()))
            throw SysError("granting access to pseudoterminal slave");
    }
#endif

    if (unlockpt(builderOut.get()))
        throw SysError("unlocking pseudoterminal");

    buildResult.startTime = time(0);

    /* Start a child process to build the derivation. */
    startChild();

    pid.setSeparatePG(true);

    processSandboxSetupMessages();

    return builderOut.get();
}

PathsInChroot DerivationBuilderImpl::getPathsInSandbox()
{
    /* Allow a user-configurable set of directories from the
       host file system. */
    PathsInChroot pathsInChroot = defaultPathsInChroot;

    if (hasPrefix(store.storeDir, tmpDirInSandbox())) {
        throw Error("`sandbox-build-dir` must not contain the storeDir");
    }
    pathsInChroot[tmpDirInSandbox()] = {.source = tmpDir};

    PathSet allowedPaths = settings.allowedImpureHostPrefixes;

    /* This works like the above, except on a per-derivation level */
    auto impurePaths = drvOptions.impureHostDeps;

    for (auto & i : impurePaths) {
        bool found = false;
        /* Note: we're not resolving symlinks here to prevent
           giving a non-root user info about inaccessible
           files. */
        Path canonI = canonPath(i);
        /* If only we had a trie to do this more efficiently :) luckily, these are generally going to be pretty small */
        for (auto & a : allowedPaths) {
            Path canonA = canonPath(a);
            if (isDirOrInDir(canonI, canonA)) {
                found = true;
                break;
            }
        }
        if (!found)
            throw Error(
                "derivation '%s' requested impure path '%s', but it was not in allowed-impure-host-deps",
                store.printStorePath(drvPath),
                i);

        /* Allow files in drvOptions.impureHostDeps to be missing; e.g.
           macOS 11+ has no /usr/lib/libSystem*.dylib */
        pathsInChroot[i] = {i, true};
    }

    if (settings.preBuildHook != "") {
        printMsg(lvlChatty, "executing pre-build hook '%1%'", settings.preBuildHook);

        enum BuildHookState { stBegin, stExtraChrootDirs };

        auto state = stBegin;
        auto lines = runProgram(settings.preBuildHook, false, getPreBuildHookArgs());
        auto lastPos = std::string::size_type{0};
        for (auto nlPos = lines.find('\n'); nlPos != std::string::npos; nlPos = lines.find('\n', lastPos)) {
            auto line = lines.substr(lastPos, nlPos - lastPos);
            lastPos = nlPos + 1;
            if (state == stBegin) {
                if (line == "extra-sandbox-paths" || line == "extra-chroot-dirs") {
                    state = stExtraChrootDirs;
                } else {
                    throw Error("unknown pre-build hook command '%1%'", line);
                }
            } else if (state == stExtraChrootDirs) {
                if (line == "") {
                    state = stBegin;
                } else {
                    auto p = line.find('=');
                    if (p == std::string::npos)
                        pathsInChroot[line] = {.source = line};
                    else
                        pathsInChroot[line.substr(0, p)] = {.source = line.substr(p + 1)};
                }
            }
        }
    }

    return pathsInChroot;
}

void DerivationBuilderImpl::prepareSandbox()
{
    if (drvOptions.useUidRange(drv))
        throw Error("feature 'uid-range' is not supported on this platform");
}

void DerivationBuilderImpl::openSlave()
{
    std::string slaveName = getPtsName(builderOut.get());

    AutoCloseFD builderOut = open(slaveName.c_str(), O_RDWR | O_NOCTTY);
    if (!builderOut)
        throw SysError("opening pseudoterminal slave");

    // Put the pt into raw mode to prevent \n -> \r\n translation.
    struct termios term;
    if (tcgetattr(builderOut.get(), &term))
        throw SysError("getting pseudoterminal attributes");

    cfmakeraw(&term);

    if (tcsetattr(builderOut.get(), TCSANOW, &term))
        throw SysError("putting pseudoterminal into raw mode");

    if (dup2(builderOut.get(), STDERR_FILENO) == -1)
        throw SysError("cannot pipe standard error into log file");
}

#if NIX_WITH_AWS_AUTH
std::optional<AwsCredentials> DerivationBuilderImpl::preResolveAwsCredentials()
{
    if (drv.isBuiltin() && drv.builder == "builtin:fetchurl") {
        auto url = drv.env.find("url");
        if (url != drv.env.end()) {
            try {
                auto parsedUrl = parseURL(url->second);
                if (parsedUrl.scheme == "s3") {
                    debug("Pre-resolving AWS credentials for S3 URL in builtin:fetchurl");
                    auto s3Url = ParsedS3URL::parse(parsedUrl);

                    // Use the preResolveAwsCredentials from aws-creds
                    auto credentials = getAwsCredentialsProvider()->getCredentials(s3Url);
                    debug("Successfully pre-resolved AWS credentials in parent process");
                    return credentials;
                }
            } catch (const std::exception & e) {
                debug("Error pre-resolving S3 credentials: %s", e.what());
            }
        }
    }
    return std::nullopt;
}
#endif

void DerivationBuilderImpl::startChild()
{
    RunChildArgs args{
#if NIX_WITH_AWS_AUTH
        .awsCredentials = preResolveAwsCredentials(),
#endif
    };

    pid = startProcess([this, args = std::move(args)]() {
        openSlave();
        runChild(std::move(args));
    });
}

void DerivationBuilderImpl::processSandboxSetupMessages()
{
    std::vector<std::string> msgs;
    while (true) {
        std::string msg = [&]() {
            try {
                return readLine(builderOut.get());
            } catch (Error & e) {
                auto status = pid.wait();
                e.addTrace(
                    {},
                    "while waiting for the build environment for '%s' to initialize (%s, previous messages: %s)",
                    store.printStorePath(drvPath),
                    statusToString(status),
                    concatStringsSep("|", msgs));
                throw;
            }
        }();
        if (msg.substr(0, 1) == "\2")
            break;
        if (msg.substr(0, 1) == "\1") {
            FdSource source(builderOut.get());
            auto ex = readError(source);
            ex.addTrace({}, "while setting up the build environment");
            throw ex;
        }
        debug("sandbox setup: " + msg);
        msgs.push_back(std::move(msg));
    }
}

void DerivationBuilderImpl::initEnv()
{
    env.clear();

    /* Most shells initialise PATH to some default (/bin:/usr/bin:...) when
       PATH is not set.  We don't want this, so we fill it in with some dummy
       value. */
    env["PATH"] = "/path-not-set";

    /* Set HOME to a non-existing path to prevent certain programs from using
       /etc/passwd (or NIS, or whatever) to locate the home directory (for
       example, wget looks for ~/.wgetrc).  I.e., these tools use /etc/passwd
       if HOME is not set, but they will just assume that the settings file
       they are looking for does not exist if HOME is set but points to some
       non-existing path. */
    env["HOME"] = homeDir;

    /* Tell the builder where the Nix store is.  Usually they
       shouldn't care, but this is useful for purity checking (e.g.,
       the compiler or linker might only want to accept paths to files
       in the store or in the build directory). */
    env["NIX_STORE"] = store.storeDir;

    /* The maximum number of cores to utilize for parallel building. */
    env["NIX_BUILD_CORES"] = fmt("%d", settings.buildCores ? settings.buildCores : settings.getDefaultCores());

    /* Write the final environment. Note that this is intentionally
       *not* `drv.env`, because we've desugared things like like
       "passAFile", "expandReferencesGraph", structured attrs, etc. */
    for (const auto & [name, info] : desugaredEnv.variables) {
        env[name] = info.prependBuildDirectory ? tmpDirInSandbox() + "/" + info.value : info.value;
    }

    /* Add extra files, similar to `finalEnv` */
    for (const auto & [fileName, value] : desugaredEnv.extraFiles) {
        writeBuilderFile(fileName, rewriteStrings(value, inputRewrites));
    }

    /* For convenience, set an environment pointing to the top build
       directory. */
    env["NIX_BUILD_TOP"] = tmpDirInSandbox();

    /* Also set TMPDIR and variants to point to this directory. */
    env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = tmpDirInSandbox();

    /* Explicitly set PWD to prevent problems with chroot builds.  In
       particular, dietlibc cannot figure out the cwd because the
       inode of the current directory doesn't appear in .. (because
       getdents returns the inode of the mount point). */
    env["PWD"] = tmpDirInSandbox();

    /* Compatibility hack with Nix <= 0.7: if this is a fixed-output
       derivation, tell the builder, so that for instance `fetchurl'
       can skip checking the output.  On older Nixes, this environment
       variable won't be set, so `fetchurl' will do the check. */
    if (derivationType.isFixed())
        env["NIX_OUTPUT_CHECKED"] = "1";

    /* *Only* if this is a fixed-output derivation, propagate the
       values of the environment variables specified in the
       `impureEnvVars' attribute to the builder.  This allows for
       instance environment variables for proxy configuration such as
       `http_proxy' to be easily passed to downloaders like
       `fetchurl'.  Passing such environment variables from the caller
       to the builder is generally impure, but the output of
       fixed-output derivations is by definition pure (since we
       already know the cryptographic hash of the output). */
    if (!derivationType.isSandboxed()) {
        auto & impureEnv = settings.impureEnv.get();
        if (!impureEnv.empty())
            experimentalFeatureSettings.require(Xp::ConfigurableImpureEnv);

        for (auto & i : drvOptions.impureEnvVars) {
            auto envVar = impureEnv.find(i);
            if (envVar != impureEnv.end()) {
                env[i] = envVar->second;
            } else {
                env[i] = getEnv(i).value_or("");
            }
        }
    }

    /* Currently structured log messages piggyback on stderr, but we
       may change that in the future. So tell the builder which file
       descriptor to use for that. */
    env["NIX_LOG_FD"] = "2";

    /* Trigger colored output in various tools. */
    env["TERM"] = "xterm-256color";
}

void DerivationBuilderImpl::startDaemon()
{
    experimentalFeatureSettings.require(Xp::RecursiveNix);

    auto store = makeRestrictedStore(
        [&] {
            auto config = make_ref<LocalStore::Config>(*this->store.config);
            config->pathInfoCacheSize = 0;
            config->stateDir = "/no-such-path";
            config->logDir = "/no-such-path";
            return config;
        }(),
        ref<LocalStore>(std::dynamic_pointer_cast<LocalStore>(this->store.shared_from_this())),
        *this);

    addedPaths.clear();

    auto socketName = ".nix-socket";
    Path socketPath = tmpDir + "/" + socketName;
    env["NIX_REMOTE"] = "unix://" + tmpDirInSandbox() + "/" + socketName;

    daemonSocket = createUnixDomainSocket(socketPath, 0600);

    chownToBuilder(socketPath);

    daemonThread = std::thread([this, store]() {
        while (true) {

            /* Accept a connection. */
            struct sockaddr_un remoteAddr;
            socklen_t remoteAddrLen = sizeof(remoteAddr);

            AutoCloseFD remote = accept(daemonSocket.get(), (struct sockaddr *) &remoteAddr, &remoteAddrLen);
            if (!remote) {
                if (errno == EINTR || errno == EAGAIN)
                    continue;
                if (errno == EINVAL || errno == ECONNABORTED)
                    break;
                throw SysError("accepting connection");
            }

            unix::closeOnExec(remote.get());

            debug("received daemon connection");

            auto workerThread = std::thread([store, remote{std::move(remote)}]() {
                try {
                    daemon::processConnection(
                        store, FdSource(remote.get()), FdSink(remote.get()), NotTrusted, daemon::Recursive);
                    debug("terminated daemon connection");
                } catch (const Interrupted &) {
                    debug("interrupted daemon connection");
                } catch (SystemError &) {
                    ignoreExceptionExceptInterrupt();
                }
            });

            daemonWorkerThreads.push_back(std::move(workerThread));
        }

        debug("daemon shutting down");
    });
}

void DerivationBuilderImpl::stopDaemon()
{
    if (daemonSocket && shutdown(daemonSocket.get(), SHUT_RDWR) == -1) {
        // According to the POSIX standard, the 'shutdown' function should
        // return an ENOTCONN error when attempting to shut down a socket that
        // hasn't been connected yet. This situation occurs when the 'accept'
        // function is called on a socket without any accepted connections,
        // leaving the socket unconnected. While Linux doesn't seem to produce
        // an error for sockets that have only been accepted, more
        // POSIX-compliant operating systems like OpenBSD, macOS, and others do
        // return the ENOTCONN error. Therefore, we handle this error here to
        // avoid raising an exception for compliant behaviour.
        if (errno == ENOTCONN) {
            daemonSocket.close();
        } else {
            throw SysError("shutting down daemon socket");
        }
    }

    if (daemonThread.joinable())
        daemonThread.join();

    // FIXME: should prune worker threads more quickly.
    // FIXME: shutdown the client socket to speed up worker termination.
    for (auto & thread : daemonWorkerThreads)
        thread.join();
    daemonWorkerThreads.clear();

    // release the socket.
    daemonSocket.close();
}

void DerivationBuilderImpl::addDependencyImpl(const StorePath & path)
{
    addedPaths.insert(path);
}

void DerivationBuilderImpl::chownToBuilder(const Path & path)
{
    if (!buildUser)
        return;
    if (chown(path.c_str(), buildUser->getUID(), buildUser->getGID()) == -1)
        throw SysError("cannot change ownership of '%1%'", path);
}

void DerivationBuilderImpl::chownToBuilder(int fd, const Path & path)
{
    if (!buildUser)
        return;
    if (fchown(fd, buildUser->getUID(), buildUser->getGID()) == -1)
        throw SysError("cannot change ownership of file '%1%'", path);
}

void DerivationBuilderImpl::writeBuilderFile(const std::string & name, std::string_view contents)
{
    auto path = std::filesystem::path(tmpDir) / name;
    AutoCloseFD fd{
        openat(tmpDirFd.get(), name.c_str(), O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC | O_EXCL | O_NOFOLLOW, 0666)};
    if (!fd)
        throw SysError("creating file %s", path);
    writeFile(fd, path, contents);
    chownToBuilder(fd.get(), path);
}

void DerivationBuilderImpl::runChild(RunChildArgs args)
{
    /* Warning: in the child we should absolutely not make any SQLite
       calls! */

    bool sendException = true;

    try { /* child */

        commonChildInit();

        /* Make the contents of netrc and the CA certificate bundle
           available to builtin:fetchurl (which may run under a
           different uid and/or in a sandbox). */
        BuiltinBuilderContext ctx{
            .drv = drv,
            .tmpDirInSandbox = tmpDirInSandbox(),
#if NIX_WITH_AWS_AUTH
            .awsCredentials = args.awsCredentials,
#endif
        };

        if (drv.isBuiltin() && drv.builder == "builtin:fetchurl") {
            try {
                ctx.netrcData = readFile(settings.netrcFile);
            } catch (SystemError &) {
            }

            try {
                ctx.caFileData = readFile(settings.caFile);
            } catch (SystemError &) {
            }
        }

        enterChroot();

        if (chdir(tmpDirInSandbox().c_str()) == -1)
            throw SysError("changing into '%1%'", tmpDir);

        /* Close all other file descriptors. */
        unix::closeExtraFDs();

        /* Disable core dumps by default. */
        struct rlimit limit = {0, RLIM_INFINITY};
        setrlimit(RLIMIT_CORE, &limit);

        // FIXME: set other limits to deterministic values?

        setUser();

        /* Indicate that we managed to set up the build environment. */
        writeFull(STDERR_FILENO, std::string("\2\n"));

        sendException = false;

        /* If this is a builtin builder, call it now. This should not return. */
        if (drv.isBuiltin()) {
            try {
                logger = makeJSONLogger(getStandardError());

                for (auto & e : drv.outputs)
                    ctx.outputs.insert_or_assign(e.first, store.printStorePath(scratchOutputs.at(e.first)));

                std::string builtinName = drv.builder.substr(8);
                assert(RegisterBuiltinBuilder::builtinBuilders);
                if (auto builtin = get(RegisterBuiltinBuilder::builtinBuilders(), builtinName))
                    (*builtin)(ctx);
                else
                    throw Error("unsupported builtin builder '%1%'", builtinName);
                _exit(0);
            } catch (std::exception & e) {
                writeFull(STDERR_FILENO, e.what() + std::string("\n"));
                _exit(1);
            }
        }

        /* It's not a builtin builder, so execute the program. */

        Strings args;
        args.push_back(std::string(baseNameOf(drv.builder)));

        for (auto & i : drv.args)
            args.push_back(rewriteStrings(i, inputRewrites));

        Strings envStrs;
        for (auto & i : env)
            envStrs.push_back(rewriteStrings(i.first + "=" + i.second, inputRewrites));

        execBuilder(args, envStrs);

        throw SysError("executing '%1%'", drv.builder);

    } catch (...) {
        handleChildException(sendException);
        _exit(1);
    }
}

void DerivationBuilderImpl::setUser()
{
    /* If we are running in `build-users' mode, then switch to the
       user we allocated above.  Make sure that we drop all root
       privileges.  Note that above we have closed all file
       descriptors except std*, so that's safe.  Also note that
       setuid() when run as root sets the real, effective and
       saved UIDs. */
    if (buildUser) {
        /* Preserve supplementary groups of the build user, to allow
           admins to specify groups such as "kvm".  */
        auto gids = buildUser->getSupplementaryGIDs();
        if (setgroups(gids.size(), gids.data()) == -1)
            throw SysError("cannot set supplementary groups of build user");

        if (setgid(buildUser->getGID()) == -1 || getgid() != buildUser->getGID() || getegid() != buildUser->getGID())
            throw SysError("setgid failed");

        if (setuid(buildUser->getUID()) == -1 || getuid() != buildUser->getUID() || geteuid() != buildUser->getUID())
            throw SysError("setuid failed");
    }
}

void DerivationBuilderImpl::execBuilder(const Strings & args, const Strings & envStrs)
{
    execve(drv.builder.c_str(), stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());
}

SingleDrvOutputs DerivationBuilderImpl::registerOutputs()
{
    std::map<std::string, ValidPathInfo> infos;

    /* Set of inodes seen during calls to canonicalisePathMetaData()
       for this build's outputs.  This needs to be shared between
       outputs to allow hard links between outputs. */
    InodesSeen inodesSeen;

    /* The paths that can be referenced are the input closures, the
       output paths, and any paths that have been built via recursive
       Nix calls. */
    StorePathSet referenceablePaths;
    for (auto & p : inputPaths)
        referenceablePaths.insert(p);
    for (auto & i : scratchOutputs)
        referenceablePaths.insert(i.second);
    for (auto & p : addedPaths)
        referenceablePaths.insert(p);

    /* Check whether the output paths were created, and make all
       output paths read-only.  Then get the references of each output (that we
       might need to register), so we can topologically sort them. For the ones
       that are most definitely already installed, we just store their final
       name so we can also use it in rewrites. */
    StringSet outputsToSort;

    struct AlreadyRegistered
    {
        StorePath path;
    };

    struct PerhapsNeedToRegister
    {
        StorePathSet refs;
    };

    std::map<std::string, std::variant<AlreadyRegistered, PerhapsNeedToRegister>> outputReferencesIfUnregistered;
    std::map<std::string, struct stat> outputStats;
    for (auto & [outputName, _] : drv.outputs) {
        auto scratchOutput = get(scratchOutputs, outputName);
        assert(scratchOutput);
        auto actualPath = realPathInSandbox(store.printStorePath(*scratchOutput));

        outputsToSort.insert(outputName);

        /* Updated wanted info to remove the outputs we definitely don't need to register */
        auto initialOutput = get(initialOutputs, outputName);
        assert(initialOutput);
        auto & initialInfo = *initialOutput;

        /* Don't register if already valid, and not checking */
        bool wanted = buildMode == bmCheck || !(initialInfo.known && initialInfo.known->isValid());
        if (!wanted) {
            outputReferencesIfUnregistered.insert_or_assign(
                outputName, AlreadyRegistered{.path = initialInfo.known->path});
            continue;
        }

        auto optSt = maybeLstat(actualPath.c_str());
        if (!optSt)
            throw BuildError(
                BuildResult::Failure::OutputRejected,
                "builder for '%s' failed to produce output path for output '%s' at '%s'",
                store.printStorePath(drvPath),
                outputName,
                actualPath);
        struct stat & st = *optSt;

#ifndef __CYGWIN__
        /* Check that the output is not group or world writable, as
           that means that someone else can have interfered with the
           build.  Also, the output should be owned by the build
           user. */
        if ((!S_ISLNK(st.st_mode) && (st.st_mode & (S_IWGRP | S_IWOTH)))
            || (buildUser && st.st_uid != buildUser->getUID()))
            throw BuildError(
                BuildResult::Failure::OutputRejected,
                "suspicious ownership or permission on '%s' for output '%s'; rejecting this build output",
                actualPath,
                outputName);
#endif

        /* Canonicalise first.  This ensures that the path we're
           rewriting doesn't contain a hard link to /etc/shadow or
           something like that. */
        canonicalisePathMetaData(
            actualPath, buildUser ? std::optional(buildUser->getUIDRange()) : std::nullopt, inodesSeen);

        bool discardReferences = false;
        if (auto udr = get(drvOptions.unsafeDiscardReferences, outputName)) {
            discardReferences = *udr;
        }

        StorePathSet references;
        if (discardReferences)
            debug("discarding references of output '%s'", outputName);
        else {
            debug("scanning for references for output '%s' in temp location '%s'", outputName, actualPath);

            /* Pass blank Sink as we are not ready to hash data at this stage. */
            NullSink blank;
            references = scanForReferences(blank, actualPath, referenceablePaths);
        }

        outputReferencesIfUnregistered.insert_or_assign(outputName, PerhapsNeedToRegister{.refs = references});
        outputStats.insert_or_assign(outputName, std::move(st));
    }

    auto topoSortResult = topoSort(outputsToSort, {[&](const std::string & name) {
                                       auto orifu = get(outputReferencesIfUnregistered, name);
                                       if (!orifu)
                                           throw BuildError(
                                               BuildResult::Failure::OutputRejected,
                                               "no output reference for '%s' in build of '%s'",
                                               name,
                                               store.printStorePath(drvPath));
                                       return std::visit(
                                           overloaded{
                                               /* Since we'll use the already installed versions of these, we
                                                  can treat them as leaves and ignore any references they
                                                  have. */
                                               [&](const AlreadyRegistered &) { return StringSet{}; },
                                               [&](const PerhapsNeedToRegister & refs) {
                                                   StringSet referencedOutputs;
                                                   /* FIXME build inverted map up front so no quadratic waste here */
                                                   for (auto & r : refs.refs)
                                                       for (auto & [o, p] : scratchOutputs)
                                                           if (r == p)
                                                               referencedOutputs.insert(o);
                                                   return referencedOutputs;
                                               },
                                           },
                                           *orifu);
                                   }});

    auto sortedOutputNames = std::visit(
        overloaded{
            [&](Cycle<std::string> & cycle) -> std::vector<std::string> {
                // TODO with more -vvvv also show the temporary paths for manual inspection.
                throw BuildError(
                    BuildResult::Failure::OutputRejected,
                    "cycle detected in build of '%s' in the references of output '%s' from output '%s'",
                    store.printStorePath(drvPath),
                    cycle.path,
                    cycle.parent);
            },
            [](auto & sorted) { return sorted; }},
        topoSortResult);

    std::reverse(sortedOutputNames.begin(), sortedOutputNames.end());

    OutputPathMap finalOutputs;

    for (auto & outputName : sortedOutputNames) {
        auto output = get(drv.outputs, outputName);
        auto scratchPath = get(scratchOutputs, outputName);
        assert(output && scratchPath);
        auto actualPath = realPathInSandbox(store.printStorePath(*scratchPath));

        auto finish = [&](StorePath finalStorePath) {
            /* Store the final path */
            finalOutputs.insert_or_assign(outputName, finalStorePath);
            /* The rewrite rule will be used in downstream outputs that refer to
               use. This is why the topological sort is essential to do first
               before this for loop. */
            if (*scratchPath != finalStorePath)
                outputRewrites[std::string{scratchPath->hashPart()}] = std::string{finalStorePath.hashPart()};
        };

        auto orifu = get(outputReferencesIfUnregistered, outputName);
        assert(orifu);

        std::optional<StorePathSet> referencesOpt = std::visit(
            overloaded{
                [&](const AlreadyRegistered & skippedFinalPath) -> std::optional<StorePathSet> {
                    finish(skippedFinalPath.path);
                    return std::nullopt;
                },
                [&](const PerhapsNeedToRegister & r) -> std::optional<StorePathSet> { return r.refs; },
            },
            *orifu);

        if (!referencesOpt)
            continue;
        auto references = *referencesOpt;

        auto rewriteOutput = [&](const StringMap & rewrites) {
            /* Apply hash rewriting if necessary. */
            if (!rewrites.empty()) {
                debug("rewriting hashes in '%1%'; cross fingers", actualPath);

                /* FIXME: Is this actually streaming? */
                auto source = sinkToSource([&](Sink & nextSink) {
                    RewritingSink rsink(rewrites, nextSink);
                    dumpPath(actualPath, rsink);
                    rsink.flush();
                });
                Path tmpPath = actualPath + ".tmp";
                restorePath(tmpPath, *source);
                deletePath(actualPath);
                movePath(tmpPath, actualPath);

                /* FIXME: set proper permissions in restorePath() so
                   we don't have to do another traversal. */
                canonicalisePathMetaData(actualPath, {}, inodesSeen);
            }
        };

        auto rewriteRefs = [&]() -> StoreReferences {
            /* In the CA case, we need the rewritten refs to calculate the
               final path, therefore we look for a *non-rewritten
               self-reference, and use a bool rather try to solve the
               computationally intractable fixed point. */
            StoreReferences res{
                .self = false,
            };
            for (auto & r : references) {
                auto name = r.name();
                auto origHash = std::string{r.hashPart()};
                if (r == *scratchPath) {
                    res.self = true;
                } else if (auto outputRewrite = get(outputRewrites, origHash)) {
                    std::string newRef = *outputRewrite;
                    newRef += '-';
                    newRef += name;
                    res.others.insert(StorePath{newRef});
                } else {
                    res.others.insert(r);
                }
            }
            return res;
        };

        auto newInfoFromCA = [&](const DerivationOutput::CAFloating outputHash) -> ValidPathInfo {
            auto st = get(outputStats, outputName);
            if (!st)
                throw BuildError(
                    BuildResult::Failure::OutputRejected, "output path %1% without valid stats info", actualPath);
            if (outputHash.method.getFileIngestionMethod() == FileIngestionMethod::Flat) {
                /* The output path should be a regular file without execute permission. */
                if (!S_ISREG(st->st_mode) || (st->st_mode & S_IXUSR) != 0)
                    throw BuildError(
                        BuildResult::Failure::OutputRejected,
                        "output path '%1%' should be a non-executable regular file "
                        "since recursive hashing is not enabled (one of outputHashMode={flat,text} is true)",
                        actualPath);
            }
            rewriteOutput(outputRewrites);
            /* FIXME optimize and deduplicate with addToStore */
            std::string oldHashPart{scratchPath->hashPart()};
            auto got = [&] {
                auto fim = outputHash.method.getFileIngestionMethod();
                switch (fim) {
                case FileIngestionMethod::Flat:
                case FileIngestionMethod::NixArchive: {
                    HashModuloSink caSink{outputHash.hashAlgo, oldHashPart};
                    auto fim = outputHash.method.getFileIngestionMethod();
                    dumpPath({getFSSourceAccessor(), CanonPath(actualPath)}, caSink, (FileSerialisationMethod) fim);
                    return caSink.finish().hash;
                }
                case FileIngestionMethod::Git: {
                    return git::dumpHash(outputHash.hashAlgo, {getFSSourceAccessor(), CanonPath(actualPath)}).hash;
                }
                }
                assert(false);
            }();

            auto newInfo0 = ValidPathInfo::makeFromCA(
                store,
                outputPathName(drv.name, outputName),
                ContentAddressWithReferences::fromParts(outputHash.method, std::move(got), rewriteRefs()),
                Hash::dummy);
            if (*scratchPath != newInfo0.path) {
                // If the path has some self-references, we need to rewrite
                // them.
                // (note that this doesn't invalidate the ca hash we calculated
                // above because it's computed *modulo the self-references*, so
                // it already takes this rewrite into account).
                rewriteOutput(StringMap{{oldHashPart, std::string(newInfo0.path.hashPart())}});
            }

            {
                HashResult narHashAndSize = hashPath(
                    {getFSSourceAccessor(), CanonPath(actualPath)},
                    FileSerialisationMethod::NixArchive,
                    HashAlgorithm::SHA256);
                newInfo0.narHash = narHashAndSize.hash;
                newInfo0.narSize = narHashAndSize.numBytesDigested;
            }

            assert(newInfo0.ca);
            return newInfo0;
        };

        ValidPathInfo newInfo = std::visit(
            overloaded{

                [&](const DerivationOutput::InputAddressed & output) {
                    /* input-addressed case */
                    auto requiredFinalPath = output.path;
                    /* Preemptively add rewrite rule for final hash, as that is
                       what the NAR hash will use rather than normalized-self references */
                    if (*scratchPath != requiredFinalPath)
                        outputRewrites.insert_or_assign(
                            std::string{scratchPath->hashPart()}, std::string{requiredFinalPath.hashPart()});
                    rewriteOutput(outputRewrites);
                    HashResult narHashAndSize = hashPath(
                        {getFSSourceAccessor(), CanonPath(actualPath)},
                        FileSerialisationMethod::NixArchive,
                        HashAlgorithm::SHA256);
                    ValidPathInfo newInfo0{requiredFinalPath, narHashAndSize.hash};
                    newInfo0.narSize = narHashAndSize.numBytesDigested;
                    auto refs = rewriteRefs();
                    newInfo0.references = std::move(refs.others);
                    if (refs.self)
                        newInfo0.references.insert(newInfo0.path);
                    return newInfo0;
                },

                [&](const DerivationOutput::CAFixed & dof) {
                    auto & wanted = dof.ca.hash;

                    // Replace the output by a fresh copy of itself to make sure
                    // that there's no stale file descriptor pointing to it
                    Path tmpOutput = actualPath + ".tmp";
                    copyFile(std::filesystem::path(actualPath), std::filesystem::path(tmpOutput), true);

                    std::filesystem::rename(tmpOutput, actualPath);

                    return newInfoFromCA(
                        DerivationOutput::CAFloating{
                            .method = dof.ca.method,
                            .hashAlgo = wanted.algo,
                        });
                },

                [&](const DerivationOutput::CAFloating & dof) { return newInfoFromCA(dof); },

                [&](const DerivationOutput::Deferred &) -> ValidPathInfo {
                    // No derivation should reach that point without having been
                    // rewritten first
                    assert(false);
                },

                [&](const DerivationOutput::Impure & doi) {
                    return newInfoFromCA(
                        DerivationOutput::CAFloating{
                            .method = doi.method,
                            .hashAlgo = doi.hashAlgo,
                        });
                },

            },
            output->raw);

        /* FIXME: set proper permissions in restorePath() so
            we don't have to do another traversal. */
        canonicalisePathMetaData(actualPath, {}, inodesSeen);

        /* Calculate where we'll move the output files. In the checking case we
           will leave leave them where they are, for now, rather than move to
           their usual "final destination" */
        auto finalDestPath = store.printStorePath(newInfo.path);

        /* Lock final output path, if not already locked. This happens with
           floating CA derivations and hash-mismatching fixed-output
           derivations. */
        PathLocks dynamicOutputLock;
        dynamicOutputLock.setDeletion(true);
        auto optFixedPath = output->path(store, drv.name, outputName);
        if (!optFixedPath || store.printStorePath(*optFixedPath) != finalDestPath) {
            assert(newInfo.ca);
            dynamicOutputLock.lockPaths({store.toRealPath(finalDestPath)});
        }

        /* Move files, if needed */
        if (store.toRealPath(finalDestPath) != actualPath) {
            if (buildMode == bmRepair) {
                /* Path already exists, need to replace it */
                replaceValidPath(store.toRealPath(finalDestPath), actualPath);
            } else if (buildMode == bmCheck) {
                /* Path already exists, and we want to compare, so we leave out
                   new path in place. */
            } else if (store.isValidPath(newInfo.path)) {
                /* Path already exists because CA path produced by something
                   else. No moving needed. */
                assert(newInfo.ca);
                /* Can delete our scratch copy now. */
                deletePath(actualPath);
            } else {
                auto destPath = store.toRealPath(finalDestPath);
                deletePath(destPath);
                movePath(actualPath, destPath);
            }
        }

        if (buildMode == bmCheck) {
            /* Check against already registered outputs */

            if (store.isValidPath(newInfo.path)) {
                ValidPathInfo oldInfo(*store.queryPathInfo(newInfo.path));
                if (newInfo.narHash != oldInfo.narHash) {
                    if (settings.runDiffHook || settings.keepFailed) {
                        auto dst = store.toRealPath(finalDestPath + ".check");
                        deletePath(dst);
                        movePath(actualPath, dst);

                        handleDiffHook(
                            buildUser ? buildUser->getUID() : getuid(),
                            buildUser ? buildUser->getGID() : getgid(),
                            finalDestPath,
                            dst,
                            store.printStorePath(drvPath),
                            tmpDir);

                        throw NotDeterministic(
                            "derivation '%s' may not be deterministic: output '%s' differs from '%s'",
                            store.printStorePath(drvPath),
                            store.toRealPath(finalDestPath),
                            dst);
                    } else
                        throw NotDeterministic(
                            "derivation '%s' may not be deterministic: output '%s' differs",
                            store.printStorePath(drvPath),
                            store.toRealPath(finalDestPath));
                }

                /* Since we verified the build, it's now ultimately trusted. */
                if (!oldInfo.ultimate) {
                    oldInfo.ultimate = true;
                    store.signPathInfo(oldInfo);
                    store.registerValidPaths({{oldInfo.path, oldInfo}});
                }
            }
        } else {
            /* do tasks relating to registering these outputs */

            /* For debugging, print out the referenced and unreferenced paths. */
            for (auto & i : inputPaths) {
                if (references.count(i))
                    debug("referenced input: '%1%'", store.printStorePath(i));
                else
                    debug("unreferenced input: '%1%'", store.printStorePath(i));
            }

            if (!store.isValidPath(newInfo.path))
                store.optimisePath(
                    store.toRealPath(finalDestPath), NoRepair); // FIXME: combine with scanForReferences()

            newInfo.deriver = drvPath;
            newInfo.ultimate = true;
            store.signPathInfo(newInfo);

            finish(newInfo.path);

            /* If it's a CA path, register it right away. This is necessary if it
               isn't statically known so that we can safely unlock the path before
               the next iteration

               This is also good so that if a fixed-output produces the
               wrong path, we still store the result (just don't consider
               the derivation sucessful, so if someone fixes the problem by
               just changing the wanted hash, the redownload (or whateer
               possibly quite slow thing it was) doesn't have to be done
               again. */
            if (newInfo.ca)
                store.registerValidPaths({{newInfo.path, newInfo}});
        }

        /* Do this in both the check and non-check cases, because we
           want `checkOutputs` below to work, which needs these path
           infos. */
        infos.emplace(outputName, std::move(newInfo));
    }

    /* Apply output checks. This includes checking of the wanted vs got
       hash of fixed-outputs. */
    checkOutputs(store, drvPath, drv.outputs, drvOptions.outputChecks, infos);

    if (buildMode == bmCheck) {
        return {};
    }

    /* Register each output path as valid, and register the sets of
       paths referenced by each of them.  If there are cycles in the
       outputs, this will fail. */
    {
        ValidPathInfos infos2;
        for (auto & [outputName, newInfo] : infos) {
            infos2.insert_or_assign(newInfo.path, newInfo);
        }
        store.registerValidPaths(infos2);
    }

    /* If we made it this far, we are sure the output matches the
       derivation That means it's safe to link the derivation to the
       output hash. We must do that for floating CA derivations, which
       otherwise couldn't be cached, but it's fine to do in all cases.
       */
    SingleDrvOutputs builtOutputs;

    for (auto & [outputName, newInfo] : infos) {
        auto oldinfo = get(initialOutputs, outputName);
        assert(oldinfo);
        auto thisRealisation = Realisation{
            {
                .outPath = newInfo.path,
            },
            DrvOutput{oldinfo->outputHash, outputName},
        };
        if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations) && !drv.type().isImpure()) {
            store.signRealisation(thisRealisation);
            store.registerDrvOutput(thisRealisation);
        }
        builtOutputs.emplace(outputName, thisRealisation);
    }

    return builtOutputs;
}

void DerivationBuilderImpl::cleanupBuild(bool force)
{
    if (force) {
        /* Delete unused redirected outputs (when doing hash rewriting). */
        for (auto & i : redirectedOutputs)
            deletePath(store.toRealPath(i.second));
    }

    if (topTmpDir != "") {
        /* As an extra precaution, even in the event of `deletePath` failing to
         * clean up, the `tmpDir` will be chowned as if we were to move
         * it inside the Nix store.
         *
         * This hardens against an attack which smuggles a file descriptor
         * to make use of the temporary directory.
         */
        chmod(topTmpDir.c_str(), 0000);

        /* Don't keep temporary directories for builtins because they
           might have privileged stuff (like a copy of netrc). */
        if (settings.keepFailed && !force && !drv.isBuiltin()) {
            printError("note: keeping build directory '%s'", tmpDir);
            chmod(topTmpDir.c_str(), 0755);
            chmod(tmpDir.c_str(), 0755);
        } else
            deletePath(topTmpDir);
        topTmpDir = "";
        tmpDir = "";
    }
}

StorePath DerivationBuilderImpl::makeFallbackPath(OutputNameView outputName)
{
    // This is a bogus path type, constructed this way to ensure that it doesn't collide with any other store path
    // See doc/manual/source/protocols/store-path.md for details
    // TODO: We may want to separate the responsibilities of constructing the path fingerprint and of actually doing the
    // hashing
    auto pathType = "rewrite:" + std::string(drvPath.to_string()) + ":name:" + std::string(outputName);
    return store.makeStorePath(
        pathType,
        // pass an all-zeroes hash
        Hash(HashAlgorithm::SHA256),
        outputPathName(drv.name, outputName));
}

StorePath DerivationBuilderImpl::makeFallbackPath(const StorePath & path)
{
    // This is a bogus path type, constructed this way to ensure that it doesn't collide with any other store path
    // See doc/manual/source/protocols/store-path.md for details
    auto pathType = "rewrite:" + std::string(drvPath.to_string()) + ":" + std::string(path.to_string());
    return store.makeStorePath(
        pathType,
        // pass an all-zeroes hash
        Hash(HashAlgorithm::SHA256),
        path.name());
}

} // namespace nix

// FIXME: do this properly
#include "chroot-derivation-builder.cc"
#include "linux-derivation-builder.cc"
#include "darwin-derivation-builder.cc"
#include "external-derivation-builder.cc"

namespace nix {

std::unique_ptr<DerivationBuilder> makeDerivationBuilder(
    LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
{
    bool useSandbox = false;

    /* Are we doing a sandboxed build? */
    {
        if (settings.sandboxMode == smEnabled) {
            if (params.drvOptions.noChroot)
                throw Error(
                    "derivation '%s' has '__noChroot' set, "
                    "but that's not allowed when 'sandbox' is 'true'",
                    store.printStorePath(params.drvPath));
#ifdef __APPLE__
            if (params.drvOptions.additionalSandboxProfile != "")
                throw Error(
                    "derivation '%s' specifies a sandbox profile, "
                    "but this is only allowed when 'sandbox' is 'relaxed'",
                    store.printStorePath(params.drvPath));
#endif
            useSandbox = true;
        } else if (settings.sandboxMode == smDisabled)
            useSandbox = false;
        else if (settings.sandboxMode == smRelaxed)
            // FIXME: cache derivationType
            useSandbox = params.drv.type().isSandboxed() && !params.drvOptions.noChroot;
    }

    if (store.storeDir != store.config->realStoreDir.get()) {
#ifdef __linux__
        useSandbox = true;
#else
        throw Error("building using a diverted store is not supported on this platform");
#endif
    }

#ifdef __linux__
    if (useSandbox && !mountAndPidNamespacesSupported()) {
        if (!settings.sandboxFallback)
            throw Error(
                "this system does not support the kernel namespaces that are required for sandboxing; use '--no-sandbox' to disable sandboxing");
        debug("auto-disabling sandboxing because the prerequisite namespaces are not available");
        useSandbox = false;
    }

#endif

    if (!useSandbox && params.drvOptions.useUidRange(params.drv))
        throw Error("feature 'uid-range' is only supported in sandboxed builds");

#ifdef __APPLE__
    return std::make_unique<DarwinDerivationBuilder>(store, std::move(miscMethods), std::move(params), useSandbox);
#elif defined(__linux__)
    if (useSandbox)
        return std::make_unique<ChrootLinuxDerivationBuilder>(store, std::move(miscMethods), std::move(params));

    return std::make_unique<LinuxDerivationBuilder>(store, std::move(miscMethods), std::move(params));
#else
    if (useSandbox)
        throw Error("sandboxing builds is not supported on this platform");

    return std::make_unique<DerivationBuilderImpl>(store, std::move(miscMethods), std::move(params));
#endif
}

} // namespace nix
