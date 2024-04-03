#include "local-derivation-goal.hh"
#include "indirect-root-store.hh"
#include "hook-instance.hh"
#include "worker.hh"
#include "builtins.hh"
#include "builtins/buildenv.hh"
#include "path-references.hh"
#include "finally.hh"
#include "util.hh"
#include "archive.hh"
#include "git.hh"
#include "compression.hh"
#include "daemon.hh"
#include "topo-sort.hh"
#include "callback.hh"
#include "json-utils.hh"
#include "cgroup.hh"
#include "personality.hh"
#include "current-process.hh"
#include "child.hh"
#include "unix-domain-socket.hh"
#include "posix-fs-canonicalise.hh"
#include "posix-source-accessor.hh"

#include <regex>
#include <queue>

#include <sys/un.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>

#if HAVE_STATVFS
#include <sys/statvfs.h>
#endif

/* Includes required for chroot support. */
#if __linux__
# include <sys/ioctl.h>
# include <net/if.h>
# include <netinet/ip.h>
# include <sys/mman.h>
# include <sched.h>
# include <sys/param.h>
# include <sys/mount.h>
# include <sys/syscall.h>
# include "namespaces.hh"
# if HAVE_SECCOMP
#   include <seccomp.h>
# endif
# define pivot_root(new_root, put_old) (syscall(SYS_pivot_root, new_root, put_old))
#endif

#if __APPLE__
#include <spawn.h>
#include <sys/sysctl.h>
#endif

#include <pwd.h>
#include <grp.h>
#include <iostream>

namespace nix {

void handleDiffHook(
    uid_t uid, uid_t gid,
    const Path & tryA, const Path & tryB,
    const Path & drvPath, const Path & tmpDir)
{
    auto & diffHookOpt = settings.diffHook.get();
    if (diffHookOpt && settings.runDiffHook) {
        auto & diffHook = *diffHookOpt;
        try {
            auto diffRes = runProgram(RunOptions {
                .program = diffHook,
                .searchPath = true,
                .args = {tryA, tryB, drvPath, tmpDir},
                .uid = uid,
                .gid = gid,
                .chdir = "/"
            });
            if (!statusOk(diffRes.first))
                throw ExecError(diffRes.first,
                    "diff-hook program '%1%' %2%",
                    diffHook,
                    statusToString(diffRes.first));

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

const Path LocalDerivationGoal::homeDir = "/homeless-shelter";


LocalDerivationGoal::~LocalDerivationGoal()
{
    /* Careful: we should never ever throw an exception from a
       destructor. */
    try { deleteTmpDir(false); } catch (...) { ignoreException(); }
    try { killChild(); } catch (...) { ignoreException(); }
    try { stopDaemon(); } catch (...) { ignoreException(); }
}


inline bool LocalDerivationGoal::needsHashRewrite()
{
#if __linux__
    return !useChroot;
#else
    /* Darwin requires hash rewriting even when sandboxing is enabled. */
    return true;
#endif
}


LocalStore & LocalDerivationGoal::getLocalStore()
{
    auto p = dynamic_cast<LocalStore *>(&worker.store);
    assert(p);
    return *p;
}


void LocalDerivationGoal::killChild()
{
    if (pid != -1) {
        worker.childTerminated(this);

        /* If we're using a build user, then there is a tricky race
           condition: if we kill the build user before the child has
           done its setuid() to the build user uid, then it won't be
           killed, and we'll potentially lock up in pid.wait().  So
           also send a conventional kill to the child. */
        ::kill(-pid, SIGKILL); /* ignore the result */

        killSandbox(true);

        pid.wait();
    }

    DerivationGoal::killChild();
}


void LocalDerivationGoal::killSandbox(bool getStats)
{
    if (cgroup) {
        #if __linux__
        auto stats = destroyCgroup(*cgroup);
        if (getStats) {
            buildResult.cpuUser = stats.cpuUser;
            buildResult.cpuSystem = stats.cpuSystem;
        }
        #else
        abort();
        #endif
    }

    else if (buildUser) {
        auto uid = buildUser->getUID();
        assert(uid != 0);
        killUser(uid);
    }
}


void LocalDerivationGoal::tryLocalBuild()
{
    unsigned int curBuilds = worker.getNrLocalBuilds();
    if (curBuilds >= settings.maxBuildJobs) {
        state = &DerivationGoal::tryToBuild;
        worker.waitForBuildSlot(shared_from_this());
        outputLocks.unlock();
        return;
    }

    assert(derivationType);

    /* Are we doing a chroot build? */
    {
        auto noChroot = parsedDrv->getBoolAttr("__noChroot");
        if (settings.sandboxMode == smEnabled) {
            if (noChroot)
                throw Error("derivation '%s' has '__noChroot' set, "
                    "but that's not allowed when 'sandbox' is 'true'", worker.store.printStorePath(drvPath));
#if __APPLE__
            if (additionalSandboxProfile != "")
                throw Error("derivation '%s' specifies a sandbox profile, "
                    "but this is only allowed when 'sandbox' is 'relaxed'", worker.store.printStorePath(drvPath));
#endif
            useChroot = true;
        }
        else if (settings.sandboxMode == smDisabled)
            useChroot = false;
        else if (settings.sandboxMode == smRelaxed)
            useChroot = derivationType->isSandboxed() && !noChroot;
    }

    auto & localStore = getLocalStore();
    if (localStore.storeDir != localStore.realStoreDir.get()) {
        #if __linux__
            useChroot = true;
        #else
            throw Error("building using a diverted store is not supported on this platform");
        #endif
    }

    #if __linux__
    if (useChroot) {
        if (!mountAndPidNamespacesSupported()) {
            if (!settings.sandboxFallback)
                throw Error("this system does not support the kernel namespaces that are required for sandboxing; use '--no-sandbox' to disable sandboxing");
            debug("auto-disabling sandboxing because the prerequisite namespaces are not available");
            useChroot = false;
        }
    }
    #endif

    if (useBuildUsers()) {
        if (!buildUser)
            buildUser = acquireUserLock(parsedDrv->useUidRange() ? 65536 : 1, useChroot);

        if (!buildUser) {
            if (!actLock)
                actLock = std::make_unique<Activity>(*logger, lvlWarn, actBuildWaiting,
                    fmt("waiting for a free build user ID for '%s'", Magenta(worker.store.printStorePath(drvPath))));
            worker.waitForAWhile(shared_from_this());
            return;
        }
    }

    actLock.reset();

    try {

        /* Okay, we have to build. */
        startBuilder();

    } catch (BuildError & e) {
        outputLocks.unlock();
        buildUser.reset();
        worker.permanentFailure = true;
        done(BuildResult::InputRejected, {}, std::move(e));
        return;
    }

    /* This state will be reached when we get EOF on the child's
       log pipe. */
    state = &DerivationGoal::buildDone;

    started();
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

    renameFile(src, dst);

    if (changePerm)
        chmod_(dst, st.st_mode);
}


extern void replaceValidPath(const Path & storePath, const Path & tmpPath);


int LocalDerivationGoal::getChildStatus()
{
    return hook ? DerivationGoal::getChildStatus() : pid.kill();
}

void LocalDerivationGoal::closeReadPipes()
{
    if (hook) {
        DerivationGoal::closeReadPipes();
    } else
        builderOut.close();
}


void LocalDerivationGoal::cleanupHookFinally()
{
    /* Release the build user at the end of this function. We don't do
       it right away because we don't want another build grabbing this
       uid and then messing around with our output. */
    buildUser.reset();
}


void LocalDerivationGoal::cleanupPreChildKill()
{
    sandboxMountNamespace = -1;
    sandboxUserNamespace = -1;
}


void LocalDerivationGoal::cleanupPostChildKill()
{
    /* When running under a build user, make sure that all processes
       running under that uid are gone.  This is to prevent a
       malicious user from leaving behind a process that keeps files
       open and modifies them after they have been chown'ed to
       root. */
    killSandbox(true);

    /* Terminate the recursive Nix daemon. */
    stopDaemon();
}


bool LocalDerivationGoal::cleanupDecideWhetherDiskFull()
{
    bool diskFull = false;

    /* Heuristically check whether the build failure may have
       been caused by a disk full condition.  We have no way
       of knowing whether the build actually got an ENOSPC.
       So instead, check if the disk is (nearly) full now.  If
       so, we don't mark this build as a permanent failure. */
#if HAVE_STATVFS
    {
        auto & localStore = getLocalStore();
        uint64_t required = 8ULL * 1024 * 1024; // FIXME: make configurable
        struct statvfs st;
        if (statvfs(localStore.realStoreDir.get().c_str(), &st) == 0 &&
            (uint64_t) st.f_bavail * st.f_bsize < required)
            diskFull = true;
        if (statvfs(tmpDir.c_str(), &st) == 0 &&
            (uint64_t) st.f_bavail * st.f_bsize < required)
            diskFull = true;
    }
#endif

    deleteTmpDir(false);

    /* Move paths out of the chroot for easier debugging of
       build failures. */
    if (useChroot && buildMode == bmNormal)
        for (auto & [_, status] : initialOutputs) {
            if (!status.known) continue;
            if (buildMode != bmCheck && status.known->isValid()) continue;
            auto p = worker.store.toRealPath(status.known->path);
            if (pathExists(chrootRootDir + p))
                renameFile((chrootRootDir + p), p);
        }

    return diskFull;
}


void LocalDerivationGoal::cleanupPostOutputsRegisteredModeCheck()
{
    deleteTmpDir(true);
}


void LocalDerivationGoal::cleanupPostOutputsRegisteredModeNonCheck()
{
    /* Delete unused redirected outputs (when doing hash rewriting). */
    for (auto & i : redirectedOutputs)
        deletePath(worker.store.Store::toRealPath(i.second));

    /* Delete the chroot (if we were using one). */
    autoDelChroot.reset(); /* this runs the destructor */

    cleanupPostOutputsRegisteredModeCheck();
}

#if __linux__
static void doBind(const Path & source, const Path & target, bool optional = false) {
    debug("bind mounting '%1%' to '%2%'", source, target);
    struct stat st;
    if (stat(source.c_str(), &st) == -1) {
        if (optional && errno == ENOENT)
            return;
        else
            throw SysError("getting attributes of path '%1%'", source);
    }
    if (S_ISDIR(st.st_mode))
        createDirs(target);
    else {
        createDirs(dirOf(target));
        writeFile(target, "");
    }
    if (mount(source.c_str(), target.c_str(), "", MS_BIND | MS_REC, 0) == -1)
        throw SysError("bind mount from '%1%' to '%2%' failed", source, target);
};
#endif

void LocalDerivationGoal::startBuilder()
{
    if ((buildUser && buildUser->getUIDCount() != 1)
        #if __linux__
        || settings.useCgroups
        #endif
        )
    {
        #if __linux__
        experimentalFeatureSettings.require(Xp::Cgroups);

        auto cgroupFS = getCgroupFS();
        if (!cgroupFS)
            throw Error("cannot determine the cgroups file system");

        auto ourCgroups = getCgroups("/proc/self/cgroup");
        auto ourCgroup = ourCgroups[""];
        if (ourCgroup == "")
            throw Error("cannot determine cgroup name from /proc/self/cgroup");

        auto ourCgroupPath = canonPath(*cgroupFS + "/" + ourCgroup);

        if (!pathExists(ourCgroupPath))
            throw Error("expected cgroup directory '%s'", ourCgroupPath);

        static std::atomic<unsigned int> counter{0};

        cgroup = buildUser
            ? fmt("%s/nix-build-uid-%d", ourCgroupPath, buildUser->getUID())
            : fmt("%s/nix-build-pid-%d-%d", ourCgroupPath, getpid(), counter++);

        debug("using cgroup '%s'", *cgroup);

        /* When using a build user, record the cgroup we used for that
           user so that if we got interrupted previously, we can kill
           any left-over cgroup first. */
        if (buildUser) {
            auto cgroupsDir = settings.nixStateDir + "/cgroups";
            createDirs(cgroupsDir);

            auto cgroupFile = fmt("%s/%d", cgroupsDir, buildUser->getUID());

            if (pathExists(cgroupFile)) {
                auto prevCgroup = readFile(cgroupFile);
                destroyCgroup(prevCgroup);
            }

            writeFile(cgroupFile, *cgroup);
        }

        #else
        throw Error("cgroups are not supported on this platform");
        #endif
    }

    /* Make sure that no other processes are executing under the
       sandbox uids. This must be done before any chownToBuilder()
       calls. */
    killSandbox(false);

    /* Right platform? */
    if (!parsedDrv->canBuildLocally(worker.store))
        throw Error("a '%s' with features {%s} is required to build '%s', but I am a '%s' with features {%s}",
            drv->platform,
            concatStringsSep(", ", parsedDrv->getRequiredSystemFeatures()),
            worker.store.printStorePath(drvPath),
            settings.thisSystem,
            concatStringsSep<StringSet>(", ", worker.store.systemFeatures));

#if __APPLE__
    additionalSandboxProfile = parsedDrv->getStringAttr("__sandboxProfile").value_or("");
#endif

    /* Create a temporary directory where the build will take
       place. */
    tmpDir = createTempDir(settings.buildDir.get().value_or(""), "nix-build-" + std::string(drvPath.name()), false, false, 0700);

    chownToBuilder(tmpDir);

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
        auto scratchPath =
            !status.known
                ? makeFallbackPath(outputName)
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
            :   /* If we are repairing or the path is totally valid, we'll need
                   to use a temporary path */
                makeFallbackPath(status.known->path);
        scratchOutputs.insert_or_assign(outputName, scratchPath);

        /* Substitute output placeholders with the scratch output paths.
           We'll use during the build. */
        inputRewrites[hashPlaceholder(outputName)] = worker.store.printStorePath(scratchPath);

        /* Additional tasks if we know the final path a priori. */
        if (!status.known) continue;
        auto fixedFinalPath = status.known->path;

        /* Additional tasks if the final and scratch are both known and
           differ. */
        if (fixedFinalPath == scratchPath) continue;

        /* Ensure scratch path is ours to use. */
        deletePath(worker.store.printStorePath(scratchPath));

        /* Rewrite and unrewrite paths */
        {
            std::string h1 { fixedFinalPath.hashPart() };
            std::string h2 { scratchPath.hashPart() };
            inputRewrites[h1] = h2;
        }

        redirectedOutputs.insert_or_assign(std::move(fixedFinalPath), std::move(scratchPath));
    }

    /* Construct the environment passed to the builder. */
    initEnv();

    writeStructuredAttrs();

    /* Handle exportReferencesGraph(), if set. */
    if (!parsedDrv->getStructuredAttrs()) {
        /* The `exportReferencesGraph' feature allows the references graph
           to be passed to a builder.  This attribute should be a list of
           pairs [name1 path1 name2 path2 ...].  The references graph of
           each `pathN' will be stored in a text file `nameN' in the
           temporary build directory.  The text files have the format used
           by `nix-store --register-validity'.  However, the deriver
           fields are left empty. */
        auto s = getOr(drv->env, "exportReferencesGraph", "");
        Strings ss = tokenizeString<Strings>(s);
        if (ss.size() % 2 != 0)
            throw BuildError("odd number of tokens in 'exportReferencesGraph': '%1%'", s);
        for (Strings::iterator i = ss.begin(); i != ss.end(); ) {
            auto fileName = *i++;
            static std::regex regex("[A-Za-z_][A-Za-z0-9_.-]*");
            if (!std::regex_match(fileName, regex))
                throw Error("invalid file name '%s' in 'exportReferencesGraph'", fileName);

            auto storePathS = *i++;
            if (!worker.store.isInStore(storePathS))
                throw BuildError("'exportReferencesGraph' contains a non-store path '%1%'", storePathS);
            auto storePath = worker.store.toStorePath(storePathS).first;

            /* Write closure info to <fileName>. */
            writeFile(tmpDir + "/" + fileName,
                worker.store.makeValidityRegistration(
                    worker.store.exportReferences({storePath}, inputPaths), false, false));
        }
    }

    if (useChroot) {

        /* Allow a user-configurable set of directories from the
           host file system. */
        pathsInChroot.clear();

        for (auto i : settings.sandboxPaths.get()) {
            if (i.empty()) continue;
            bool optional = false;
            if (i[i.size() - 1] == '?') {
                optional = true;
                i.pop_back();
            }
            size_t p = i.find('=');
            if (p == std::string::npos)
                pathsInChroot[i] = {i, optional};
            else
                pathsInChroot[i.substr(0, p)] = {i.substr(p + 1), optional};
        }
        if (hasPrefix(worker.store.storeDir, tmpDirInSandbox))
        {
            throw Error("`sandbox-build-dir` must not contain the storeDir");
        }
        pathsInChroot[tmpDirInSandbox] = tmpDir;

        /* Add the closure of store paths to the chroot. */
        StorePathSet closure;
        for (auto & i : pathsInChroot)
            try {
                if (worker.store.isInStore(i.second.source))
                    worker.store.computeFSClosure(worker.store.toStorePath(i.second.source).first, closure);
            } catch (InvalidPath & e) {
            } catch (Error & e) {
                e.addTrace({}, "while processing 'sandbox-paths'");
                throw;
            }
        for (auto & i : closure) {
            auto p = worker.store.printStorePath(i);
            pathsInChroot.insert_or_assign(p, p);
        }

        PathSet allowedPaths = settings.allowedImpureHostPrefixes;

        /* This works like the above, except on a per-derivation level */
        auto impurePaths = parsedDrv->getStringsAttr("__impureHostDeps").value_or(Strings());

        for (auto & i : impurePaths) {
            bool found = false;
            /* Note: we're not resolving symlinks here to prevent
               giving a non-root user info about inaccessible
               files. */
            Path canonI = canonPath(i);
            /* If only we had a trie to do this more efficiently :) luckily, these are generally going to be pretty small */
            for (auto & a : allowedPaths) {
                Path canonA = canonPath(a);
                if (canonI == canonA || isInDir(canonI, canonA)) {
                    found = true;
                    break;
                }
            }
            if (!found)
                throw Error("derivation '%s' requested impure path '%s', but it was not in allowed-impure-host-deps",
                    worker.store.printStorePath(drvPath), i);

            /* Allow files in __impureHostDeps to be missing; e.g.
               macOS 11+ has no /usr/lib/libSystem*.dylib */
            pathsInChroot[i] = {i, true};
        }

#if __linux__
        /* Create a temporary directory in which we set up the chroot
           environment using bind-mounts.  We put it in the Nix store
           so that the build outputs can be moved efficiently from the
           chroot to their final location. */
        chrootRootDir = worker.store.Store::toRealPath(drvPath) + ".chroot";
        deletePath(chrootRootDir);

        /* Clean up the chroot directory automatically. */
        autoDelChroot = std::make_shared<AutoDelete>(chrootRootDir);

        printMsg(lvlChatty, "setting up chroot environment in '%1%'", chrootRootDir);

        // FIXME: make this 0700
        if (mkdir(chrootRootDir.c_str(), buildUser && buildUser->getUIDCount() != 1 ? 0755 : 0750) == -1)
            throw SysError("cannot create '%1%'", chrootRootDir);

        if (buildUser && chown(chrootRootDir.c_str(), buildUser->getUIDCount() != 1 ? buildUser->getUID() : 0, buildUser->getGID()) == -1)
            throw SysError("cannot change ownership of '%1%'", chrootRootDir);

        /* Create a writable /tmp in the chroot.  Many builders need
           this.  (Of course they should really respect $TMPDIR
           instead.) */
        Path chrootTmpDir = chrootRootDir + "/tmp";
        createDirs(chrootTmpDir);
        chmod_(chrootTmpDir, 01777);

        /* Create a /etc/passwd with entries for the build user and the
           nobody account.  The latter is kind of a hack to support
           Samba-in-QEMU. */
        createDirs(chrootRootDir + "/etc");
        if (parsedDrv->useUidRange())
            chownToBuilder(chrootRootDir + "/etc");

        if (parsedDrv->useUidRange() && (!buildUser || buildUser->getUIDCount() < 65536))
            throw Error("feature 'uid-range' requires the setting '%s' to be enabled", settings.autoAllocateUids.name);

        /* Declare the build user's group so that programs get a consistent
           view of the system (e.g., "id -gn"). */
        writeFile(chrootRootDir + "/etc/group",
            fmt("root:x:0:\n"
                "nixbld:!:%1%:\n"
                "nogroup:x:65534:\n", sandboxGid()));

        /* Create /etc/hosts with localhost entry. */
        if (derivationType->isSandboxed())
            writeFile(chrootRootDir + "/etc/hosts", "127.0.0.1 localhost\n::1 localhost\n");

        /* Make the closure of the inputs available in the chroot,
           rather than the whole Nix store.  This prevents any access
           to undeclared dependencies.  Directories are bind-mounted,
           while other inputs are hard-linked (since only directories
           can be bind-mounted).  !!! As an extra security
           precaution, make the fake Nix store only writable by the
           build user. */
        Path chrootStoreDir = chrootRootDir + worker.store.storeDir;
        createDirs(chrootStoreDir);
        chmod_(chrootStoreDir, 01775);

        if (buildUser && chown(chrootStoreDir.c_str(), 0, buildUser->getGID()) == -1)
            throw SysError("cannot change ownership of '%1%'", chrootStoreDir);

        for (auto & i : inputPaths) {
            auto p = worker.store.printStorePath(i);
            Path r = worker.store.toRealPath(p);
            pathsInChroot.insert_or_assign(p, r);
        }

        /* If we're repairing, checking or rebuilding part of a
           multiple-outputs derivation, it's possible that we're
           rebuilding a path that is in settings.sandbox-paths
           (typically the dependencies of /bin/sh).  Throw them
           out. */
        for (auto & i : drv->outputsAndOptPaths(worker.store)) {
            /* If the name isn't known a priori (i.e. floating
               content-addressed derivation), the temporary location we use
               should be fresh.  Freshness means it is impossible that the path
               is already in the sandbox, so we don't need to worry about
               removing it.  */
            if (i.second.second)
                pathsInChroot.erase(worker.store.printStorePath(*i.second.second));
        }

        if (cgroup) {
            if (mkdir(cgroup->c_str(), 0755) != 0)
                throw SysError("creating cgroup '%s'", *cgroup);
            chownToBuilder(*cgroup);
            chownToBuilder(*cgroup + "/cgroup.procs");
            chownToBuilder(*cgroup + "/cgroup.threads");
            //chownToBuilder(*cgroup + "/cgroup.subtree_control");
        }

#else
        if (parsedDrv->useUidRange())
            throw Error("feature 'uid-range' is not supported on this platform");
        #if __APPLE__
            /* We don't really have any parent prep work to do (yet?)
               All work happens in the child, instead. */
        #else
            throw Error("sandboxing builds is not supported on this platform");
        #endif
#endif
    } else {
        if (parsedDrv->useUidRange())
            throw Error("feature 'uid-range' is only supported in sandboxed builds");
    }

    if (needsHashRewrite() && pathExists(homeDir))
        throw Error("home directory '%1%' exists; please remove it to assure purity of builds without sandboxing", homeDir);

    if (useChroot && settings.preBuildHook != "" && dynamic_cast<Derivation *>(drv.get())) {
        printMsg(lvlChatty, "executing pre-build hook '%1%'", settings.preBuildHook);
        auto args = useChroot ? Strings({worker.store.printStorePath(drvPath), chrootRootDir}) :
            Strings({ worker.store.printStorePath(drvPath) });
        enum BuildHookState {
            stBegin,
            stExtraChrootDirs
        };
        auto state = stBegin;
        auto lines = runProgram(settings.preBuildHook, false, args);
        auto lastPos = std::string::size_type{0};
        for (auto nlPos = lines.find('\n'); nlPos != std::string::npos;
                nlPos = lines.find('\n', lastPos))
        {
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
                        pathsInChroot[line] = line;
                    else
                        pathsInChroot[line.substr(0, p)] = line.substr(p + 1);
                }
            }
        }
    }

    /* Fire up a Nix daemon to process recursive Nix calls from the
       builder. */
    if (parsedDrv->getRequiredSystemFeatures().count("recursive-nix"))
        startDaemon();

    /* Run the builder. */
    printMsg(lvlChatty, "executing builder '%1%'", drv->builder);
    printMsg(lvlChatty, "using builder args '%1%'", concatStringsSep(" ", drv->args));
    for (auto & i : drv->env)
        printMsg(lvlVomit, "setting builder env variable '%1%'='%2%'", i.first, i.second);

    /* Create the log file. */
    Path logFile = openLogFile();

    /* Create a pseudoterminal to get the output of the builder. */
    builderOut = posix_openpt(O_RDWR | O_NOCTTY);
    if (!builderOut)
        throw SysError("opening pseudoterminal master");

    // FIXME: not thread-safe, use ptsname_r
    std::string slaveName = ptsname(builderOut.get());

    if (buildUser) {
        if (chmod(slaveName.c_str(), 0600))
            throw SysError("changing mode of pseudoterminal slave");

        if (chown(slaveName.c_str(), buildUser->getUID(), 0))
            throw SysError("changing owner of pseudoterminal slave");
    }
#if __APPLE__
    else {
        if (grantpt(builderOut.get()))
            throw SysError("granting access to pseudoterminal slave");
    }
#endif

    if (unlockpt(builderOut.get()))
        throw SysError("unlocking pseudoterminal");

    /* Open the slave side of the pseudoterminal and use it as stderr. */
    auto openSlave = [&]()
    {
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
    };

    buildResult.startTime = time(0);

    /* Fork a child to build the package. */

#if __linux__
    if (useChroot) {
        /* Set up private namespaces for the build:

           - The PID namespace causes the build to start as PID 1.
             Processes outside of the chroot are not visible to those
             on the inside, but processes inside the chroot are
             visible from the outside (though with different PIDs).

           - The private mount namespace ensures that all the bind
             mounts we do will only show up in this process and its
             children, and will disappear automatically when we're
             done.

           - The private network namespace ensures that the builder
             cannot talk to the outside world (or vice versa).  It
             only has a private loopback interface. (Fixed-output
             derivations are not run in a private network namespace
             to allow functions like fetchurl to work.)

           - The IPC namespace prevents the builder from communicating
             with outside processes using SysV IPC mechanisms (shared
             memory, message queues, semaphores).  It also ensures
             that all IPC objects are destroyed when the builder
             exits.

           - The UTS namespace ensures that builders see a hostname of
             localhost rather than the actual hostname.

           We use a helper process to do the clone() to work around
           clone() being broken in multi-threaded programs due to
           at-fork handlers not being run. Note that we use
           CLONE_PARENT to ensure that the real builder is parented to
           us.
        */

        if (derivationType->isSandboxed())
            privateNetwork = true;

        userNamespaceSync.create();

        usingUserNamespace = userNamespacesSupported();

        Pipe sendPid;
        sendPid.create();

        Pid helper = startProcess([&]() {
            sendPid.readSide.close();

            /* We need to open the slave early, before
               CLONE_NEWUSER. Otherwise we get EPERM when running as
               root. */
            openSlave();

            /* Drop additional groups here because we can't do it
               after we've created the new user namespace. */
            if (setgroups(0, 0) == -1) {
                if (errno != EPERM)
                    throw SysError("setgroups failed");
                if (settings.requireDropSupplementaryGroups)
                    throw Error("setgroups failed. Set the require-drop-supplementary-groups option to false to skip this step.");
            }

            ProcessOptions options;
            options.cloneFlags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS | CLONE_PARENT | SIGCHLD;
            if (privateNetwork)
                options.cloneFlags |= CLONE_NEWNET;
            if (usingUserNamespace)
                options.cloneFlags |= CLONE_NEWUSER;

            pid_t child = startProcess([&]() { runChild(); }, options);

            writeFull(sendPid.writeSide.get(), fmt("%d\n", child));
            _exit(0);
        });

        sendPid.writeSide.close();

        if (helper.wait() != 0)
            throw Error("unable to start build process");

        userNamespaceSync.readSide = -1;

        /* Close the write side to prevent runChild() from hanging
           reading from this. */
        Finally cleanup([&]() {
            userNamespaceSync.writeSide = -1;
        });

        auto ss = tokenizeString<std::vector<std::string>>(readLine(sendPid.readSide.get()));
        assert(ss.size() == 1);
        pid = string2Int<pid_t>(ss[0]).value();

        if (usingUserNamespace) {
            /* Set the UID/GID mapping of the builder's user namespace
               such that the sandbox user maps to the build user, or to
               the calling user (if build users are disabled). */
            uid_t hostUid = buildUser ? buildUser->getUID() : getuid();
            uid_t hostGid = buildUser ? buildUser->getGID() : getgid();
            uid_t nrIds = buildUser ? buildUser->getUIDCount() : 1;

            writeFile("/proc/" + std::to_string(pid) + "/uid_map",
                fmt("%d %d %d", sandboxUid(), hostUid, nrIds));

            if (!buildUser || buildUser->getUIDCount() == 1)
                writeFile("/proc/" + std::to_string(pid) + "/setgroups", "deny");

            writeFile("/proc/" + std::to_string(pid) + "/gid_map",
                fmt("%d %d %d", sandboxGid(), hostGid, nrIds));
        } else {
            debug("note: not using a user namespace");
            if (!buildUser)
                throw Error("cannot perform a sandboxed build because user namespaces are not enabled; check /proc/sys/user/max_user_namespaces");
        }

        /* Now that we now the sandbox uid, we can write
           /etc/passwd. */
        writeFile(chrootRootDir + "/etc/passwd", fmt(
                "root:x:0:0:Nix build user:%3%:/noshell\n"
                "nixbld:x:%1%:%2%:Nix build user:%3%:/noshell\n"
                "nobody:x:65534:65534:Nobody:/:/noshell\n",
                sandboxUid(), sandboxGid(), settings.sandboxBuildDir));

        /* Save the mount- and user namespace of the child. We have to do this
           *before* the child does a chroot. */
        sandboxMountNamespace = open(fmt("/proc/%d/ns/mnt", (pid_t) pid).c_str(), O_RDONLY);
        if (sandboxMountNamespace.get() == -1)
            throw SysError("getting sandbox mount namespace");

        if (usingUserNamespace) {
            sandboxUserNamespace = open(fmt("/proc/%d/ns/user", (pid_t) pid).c_str(), O_RDONLY);
            if (sandboxUserNamespace.get() == -1)
                throw SysError("getting sandbox user namespace");
        }

        /* Move the child into its own cgroup. */
        if (cgroup)
            writeFile(*cgroup + "/cgroup.procs", fmt("%d", (pid_t) pid));

        /* Signal the builder that we've updated its user namespace. */
        writeFull(userNamespaceSync.writeSide.get(), "1");

    } else
#endif
    {
        pid = startProcess([&]() {
            openSlave();
            runChild();
        });
    }

    /* parent */
    pid.setSeparatePG(true);
    worker.childStarted(shared_from_this(), {builderOut.get()}, true, true);

    /* Check if setting up the build environment failed. */
    std::vector<std::string> msgs;
    while (true) {
        std::string msg = [&]() {
            try {
                return readLine(builderOut.get());
            } catch (Error & e) {
                auto status = pid.wait();
                e.addTrace({}, "while waiting for the build environment for '%s' to initialize (%s, previous messages: %s)",
                    worker.store.printStorePath(drvPath),
                    statusToString(status),
                    concatStringsSep("|", msgs));
                throw;
            }
        }();
        if (msg.substr(0, 1) == "\2") break;
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


void LocalDerivationGoal::initTmpDir() {
    /* In a sandbox, for determinism, always use the same temporary
       directory. */
#if __linux__
    tmpDirInSandbox = useChroot ? settings.sandboxBuildDir : tmpDir;
#else
    tmpDirInSandbox = tmpDir;
#endif

    /* In non-structured mode, add all bindings specified in the
       derivation via the environment, except those listed in the
       passAsFile attribute. Those are passed as file names pointing
       to temporary files containing the contents. Note that
       passAsFile is ignored in structure mode because it's not
       needed (attributes are not passed through the environment, so
       there is no size constraint). */
    if (!parsedDrv->getStructuredAttrs()) {

        StringSet passAsFile = tokenizeString<StringSet>(getOr(drv->env, "passAsFile", ""));
        for (auto & i : drv->env) {
            if (passAsFile.find(i.first) == passAsFile.end()) {
                env[i.first] = i.second;
            } else {
                auto hash = hashString(HashAlgorithm::SHA256, i.first);
                std::string fn = ".attr-" + hash.to_string(HashFormat::Nix32, false);
                Path p = tmpDir + "/" + fn;
                writeFile(p, rewriteStrings(i.second, inputRewrites));
                chownToBuilder(p);
                env[i.first + "Path"] = tmpDirInSandbox + "/" + fn;
            }
        }

    }

    /* For convenience, set an environment pointing to the top build
       directory. */
    env["NIX_BUILD_TOP"] = tmpDirInSandbox;

    /* Also set TMPDIR and variants to point to this directory. */
    env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = tmpDirInSandbox;

    /* Explicitly set PWD to prevent problems with chroot builds.  In
       particular, dietlibc cannot figure out the cwd because the
       inode of the current directory doesn't appear in .. (because
       getdents returns the inode of the mount point). */
    env["PWD"] = tmpDirInSandbox;
}


void LocalDerivationGoal::initEnv()
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
    env["NIX_STORE"] = worker.store.storeDir;

    /* The maximum number of cores to utilize for parallel building. */
    env["NIX_BUILD_CORES"] = fmt("%d", settings.buildCores);

    initTmpDir();

    /* Compatibility hack with Nix <= 0.7: if this is a fixed-output
       derivation, tell the builder, so that for instance `fetchurl'
       can skip checking the output.  On older Nixes, this environment
       variable won't be set, so `fetchurl' will do the check. */
    if (derivationType->isFixed()) env["NIX_OUTPUT_CHECKED"] = "1";

    /* *Only* if this is a fixed-output derivation, propagate the
       values of the environment variables specified in the
       `impureEnvVars' attribute to the builder.  This allows for
       instance environment variables for proxy configuration such as
       `http_proxy' to be easily passed to downloaders like
       `fetchurl'.  Passing such environment variables from the caller
       to the builder is generally impure, but the output of
       fixed-output derivations is by definition pure (since we
       already know the cryptographic hash of the output). */
    if (!derivationType->isSandboxed()) {
        auto & impureEnv = settings.impureEnv.get();
        if (!impureEnv.empty())
            experimentalFeatureSettings.require(Xp::ConfigurableImpureEnv);

        for (auto & i : parsedDrv->getStringsAttr("impureEnvVars").value_or(Strings())) {
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


void LocalDerivationGoal::writeStructuredAttrs()
{
    if (auto structAttrsJson = parsedDrv->prepareStructuredAttrs(worker.store, inputPaths)) {
        auto json = structAttrsJson.value();
        nlohmann::json rewritten;
        for (auto & [i, v] : json["outputs"].get<nlohmann::json::object_t>()) {
            /* The placeholder must have a rewrite, so we use it to cover both the
               cases where we know or don't know the output path ahead of time. */
            rewritten[i] = rewriteStrings((std::string) v, inputRewrites);
        }

        json["outputs"] = rewritten;

        auto jsonSh = writeStructuredAttrsShell(json);

        writeFile(tmpDir + "/.attrs.sh", rewriteStrings(jsonSh, inputRewrites));
        chownToBuilder(tmpDir + "/.attrs.sh");
        env["NIX_ATTRS_SH_FILE"] = tmpDirInSandbox + "/.attrs.sh";
        writeFile(tmpDir + "/.attrs.json", rewriteStrings(json.dump(), inputRewrites));
        chownToBuilder(tmpDir + "/.attrs.json");
        env["NIX_ATTRS_JSON_FILE"] = tmpDirInSandbox + "/.attrs.json";
    }
}


static StorePath pathPartOfReq(const SingleDerivedPath & req)
{
    return std::visit(overloaded {
        [&](const SingleDerivedPath::Opaque & bo) {
            return bo.path;
        },
        [&](const SingleDerivedPath::Built & bfd)  {
            return pathPartOfReq(*bfd.drvPath);
        },
    }, req.raw());
}


static StorePath pathPartOfReq(const DerivedPath & req)
{
    return std::visit(overloaded {
        [&](const DerivedPath::Opaque & bo) {
            return bo.path;
        },
        [&](const DerivedPath::Built & bfd)  {
            return pathPartOfReq(*bfd.drvPath);
        },
    }, req.raw());
}


bool LocalDerivationGoal::isAllowed(const DerivedPath & req)
{
    return this->isAllowed(pathPartOfReq(req));
}


struct RestrictedStoreConfig : virtual LocalFSStoreConfig
{
    using LocalFSStoreConfig::LocalFSStoreConfig;
    const std::string name() { return "Restricted Store"; }
};

/* A wrapper around LocalStore that only allows building/querying of
   paths that are in the input closures of the build or were added via
   recursive Nix calls. */
struct RestrictedStore : public virtual RestrictedStoreConfig, public virtual IndirectRootStore, public virtual GcStore
{
    ref<LocalStore> next;

    LocalDerivationGoal & goal;

    RestrictedStore(const Params & params, ref<LocalStore> next, LocalDerivationGoal & goal)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
        , RestrictedStoreConfig(params)
        , Store(params)
        , LocalFSStore(params)
        , next(next), goal(goal)
    { }

    Path getRealStoreDir() override
    { return next->realStoreDir; }

    std::string getUri() override
    { return next->getUri(); }

    StorePathSet queryAllValidPaths() override
    {
        StorePathSet paths;
        for (auto & p : goal.inputPaths) paths.insert(p);
        for (auto & p : goal.addedPaths) paths.insert(p);
        return paths;
    }

    void queryPathInfoUncached(const StorePath & path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override
    {
        if (goal.isAllowed(path)) {
            try {
                /* Censor impure information. */
                auto info = std::make_shared<ValidPathInfo>(*next->queryPathInfo(path));
                info->deriver.reset();
                info->registrationTime = 0;
                info->ultimate = false;
                info->sigs.clear();
                callback(info);
            } catch (InvalidPath &) {
                callback(nullptr);
            }
        } else
            callback(nullptr);
    };

    void queryReferrers(const StorePath & path, StorePathSet & referrers) override
    { }

    std::map<std::string, std::optional<StorePath>> queryPartialDerivationOutputMap(
        const StorePath & path,
        Store * evalStore = nullptr) override
    {
        if (!goal.isAllowed(path))
            throw InvalidPath("cannot query output map for unknown path '%s' in recursive Nix", printStorePath(path));
        return next->queryPartialDerivationOutputMap(path, evalStore);
    }

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    { throw Error("queryPathFromHashPart"); }

    StorePath addToStore(
        std::string_view name,
        SourceAccessor & accessor,
        const CanonPath & srcPath,
        ContentAddressMethod method,
        HashAlgorithm hashAlgo,
        const StorePathSet & references,
        PathFilter & filter,
        RepairFlag repair) override
    { throw Error("addToStore"); }

    void addToStore(const ValidPathInfo & info, Source & narSource,
        RepairFlag repair = NoRepair, CheckSigsFlag checkSigs = CheckSigs) override
    {
        next->addToStore(info, narSource, repair, checkSigs);
        goal.addDependency(info.path);
    }

    StorePath addToStoreFromDump(
        Source & dump,
        std::string_view name,
        FileSerialisationMethod dumpMethod,
        ContentAddressMethod hashMethod,
        HashAlgorithm hashAlgo,
        const StorePathSet & references,
        RepairFlag repair) override
    {
        auto path = next->addToStoreFromDump(dump, name, dumpMethod, hashMethod, hashAlgo, references, repair);
        goal.addDependency(path);
        return path;
    }

    void narFromPath(const StorePath & path, Sink & sink) override
    {
        if (!goal.isAllowed(path))
            throw InvalidPath("cannot dump unknown path '%s' in recursive Nix", printStorePath(path));
        LocalFSStore::narFromPath(path, sink);
    }

    void ensurePath(const StorePath & path) override
    {
        if (!goal.isAllowed(path))
            throw InvalidPath("cannot substitute unknown path '%s' in recursive Nix", printStorePath(path));
        /* Nothing to be done; 'path' must already be valid. */
    }

    void registerDrvOutput(const Realisation & info) override
    // XXX: This should probably be allowed as a no-op if the realisation
    // corresponds to an allowed derivation
    { throw Error("registerDrvOutput"); }

    void queryRealisationUncached(const DrvOutput & id,
        Callback<std::shared_ptr<const Realisation>> callback) noexcept override
    // XXX: This should probably be allowed if the realisation corresponds to
    // an allowed derivation
    {
        if (!goal.isAllowed(id))
            callback(nullptr);
        next->queryRealisation(id, std::move(callback));
    }

    void buildPaths(const std::vector<DerivedPath> & paths, BuildMode buildMode, std::shared_ptr<Store> evalStore) override
    {
        for (auto & result : buildPathsWithResults(paths, buildMode, evalStore))
            if (!result.success())
                result.rethrow();
    }

    std::vector<KeyedBuildResult> buildPathsWithResults(
        const std::vector<DerivedPath> & paths,
        BuildMode buildMode = bmNormal,
        std::shared_ptr<Store> evalStore = nullptr) override
    {
        assert(!evalStore);

        if (buildMode != bmNormal) throw Error("unsupported build mode");

        StorePathSet newPaths;
        std::set<Realisation> newRealisations;

        for (auto & req : paths) {
            if (!goal.isAllowed(req))
                throw InvalidPath("cannot build '%s' in recursive Nix because path is unknown", req.to_string(*next));
        }

        auto results = next->buildPathsWithResults(paths, buildMode);

        for (auto & result : results) {
            for (auto & [outputName, output] : result.builtOutputs) {
                newPaths.insert(output.outPath);
                newRealisations.insert(output);
            }
        }

        StorePathSet closure;
        next->computeFSClosure(newPaths, closure);
        for (auto & path : closure)
            goal.addDependency(path);
        for (auto & real : Realisation::closure(*next, newRealisations))
            goal.addedDrvOutputs.insert(real.id);

        return results;
    }

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
        BuildMode buildMode = bmNormal) override
    { unsupported("buildDerivation"); }

    void addTempRoot(const StorePath & path) override
    { }

    void addIndirectRoot(const Path & path) override
    { }

    Roots findRoots(bool censor) override
    { return Roots(); }

    void collectGarbage(const GCOptions & options, GCResults & results) override
    { }

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override
    { unsupported("addSignatures"); }

    void queryMissing(const std::vector<DerivedPath> & targets,
        StorePathSet & willBuild, StorePathSet & willSubstitute, StorePathSet & unknown,
        uint64_t & downloadSize, uint64_t & narSize) override
    {
        /* This is slightly impure since it leaks information to the
           client about what paths will be built/substituted or are
           already present. Probably not a big deal. */

        std::vector<DerivedPath> allowed;
        for (auto & req : targets) {
            if (goal.isAllowed(req))
                allowed.emplace_back(req);
            else
                unknown.insert(pathPartOfReq(req));
        }

        next->queryMissing(allowed, willBuild, willSubstitute,
            unknown, downloadSize, narSize);
    }

    virtual std::optional<std::string> getBuildLogExact(const StorePath & path) override
    { return std::nullopt; }

    virtual void addBuildLog(const StorePath & path, std::string_view log) override
    { unsupported("addBuildLog"); }

    std::optional<TrustedFlag> isTrustedClient() override
    { return NotTrusted; }
};


void LocalDerivationGoal::startDaemon()
{
    experimentalFeatureSettings.require(Xp::RecursiveNix);

    Store::Params params;
    params["path-info-cache-size"] = "0";
    params["store"] = worker.store.storeDir;
    if (auto & optRoot = getLocalStore().rootDir.get())
        params["root"] = *optRoot;
    params["state"] = "/no-such-path";
    params["log"] = "/no-such-path";
    auto store = make_ref<RestrictedStore>(params,
        ref<LocalStore>(std::dynamic_pointer_cast<LocalStore>(worker.store.shared_from_this())),
        *this);

    addedPaths.clear();

    auto socketName = ".nix-socket";
    Path socketPath = tmpDir + "/" + socketName;
    env["NIX_REMOTE"] = "unix://" + tmpDirInSandbox + "/" + socketName;

    daemonSocket = createUnixDomainSocket(socketPath, 0600);

    chownToBuilder(socketPath);

    daemonThread = std::thread([this, store]() {

        while (true) {

            /* Accept a connection. */
            struct sockaddr_un remoteAddr;
            socklen_t remoteAddrLen = sizeof(remoteAddr);

            AutoCloseFD remote = accept(daemonSocket.get(),
                (struct sockaddr *) &remoteAddr, &remoteAddrLen);
            if (!remote) {
                if (errno == EINTR || errno == EAGAIN) continue;
                if (errno == EINVAL || errno == ECONNABORTED) break;
                throw SysError("accepting connection");
            }

            closeOnExec(remote.get());

            debug("received daemon connection");

            auto workerThread = std::thread([store, remote{std::move(remote)}]() {
                FdSource from(remote.get());
                FdSink to(remote.get());
                try {
                    daemon::processConnection(store, from, to,
                        NotTrusted, daemon::Recursive);
                    debug("terminated daemon connection");
                } catch (SystemError &) {
                    ignoreException();
                }
            });

            daemonWorkerThreads.push_back(std::move(workerThread));
        }

        debug("daemon shutting down");
    });
}


void LocalDerivationGoal::stopDaemon()
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


void LocalDerivationGoal::addDependency(const StorePath & path)
{
    if (isAllowed(path)) return;

    addedPaths.insert(path);

    /* If we're doing a sandbox build, then we have to make the path
       appear in the sandbox. */
    if (useChroot) {

        debug("materialising '%s' in the sandbox", worker.store.printStorePath(path));

        #if __linux__

            Path source = worker.store.Store::toRealPath(path);
            Path target = chrootRootDir + worker.store.printStorePath(path);

            if (pathExists(target)) {
                // There is a similar debug message in doBind, so only run it in this block to not have double messages.
                debug("bind-mounting %s -> %s", target, source);
                throw Error("store path '%s' already exists in the sandbox", worker.store.printStorePath(path));
            }

            /* Bind-mount the path into the sandbox. This requires
               entering its mount namespace, which is not possible
               in multithreaded programs. So we do this in a
               child process.*/
            Pid child(startProcess([&]() {

                if (usingUserNamespace && (setns(sandboxUserNamespace.get(), 0) == -1))
                    throw SysError("entering sandbox user namespace");

                if (setns(sandboxMountNamespace.get(), 0) == -1)
                    throw SysError("entering sandbox mount namespace");

                doBind(source, target);

                _exit(0);
            }));

            int status = child.wait();
            if (status != 0)
                throw Error("could not add path '%s' to sandbox", worker.store.printStorePath(path));

        #else
            throw Error("don't know how to make path '%s' (produced by a recursive Nix call) appear in the sandbox",
                worker.store.printStorePath(path));
        #endif

    }
}

void LocalDerivationGoal::chownToBuilder(const Path & path)
{
    if (!buildUser) return;
    if (chown(path.c_str(), buildUser->getUID(), buildUser->getGID()) == -1)
        throw SysError("cannot change ownership of '%1%'", path);
}


void setupSeccomp()
{
#if __linux__
    if (!settings.filterSyscalls) return;
#if HAVE_SECCOMP
    scmp_filter_ctx ctx;

    if (!(ctx = seccomp_init(SCMP_ACT_ALLOW)))
        throw SysError("unable to initialize seccomp mode 2");

    Finally cleanup([&]() {
        seccomp_release(ctx);
    });

    constexpr std::string_view nativeSystem = SYSTEM;

    if (nativeSystem == "x86_64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_X86) != 0)
        throw SysError("unable to add 32-bit seccomp architecture");

    if (nativeSystem == "x86_64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_X32) != 0)
        throw SysError("unable to add X32 seccomp architecture");

    if (nativeSystem == "aarch64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_ARM) != 0)
        printError("unable to add ARM seccomp architecture; this may result in spurious build failures if running 32-bit ARM processes");

    if (nativeSystem == "mips64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_MIPS) != 0)
        printError("unable to add mips seccomp architecture");

    if (nativeSystem == "mips64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_MIPS64N32) != 0)
        printError("unable to add mips64-*abin32 seccomp architecture");

    if (nativeSystem == "mips64el-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_MIPSEL) != 0)
        printError("unable to add mipsel seccomp architecture");

    if (nativeSystem == "mips64el-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_MIPSEL64N32) != 0)
        printError("unable to add mips64el-*abin32 seccomp architecture");

    /* Prevent builders from creating setuid/setgid binaries. */
    for (int perm : { S_ISUID, S_ISGID }) {
        if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(chmod), 1,
                SCMP_A1(SCMP_CMP_MASKED_EQ, (scmp_datum_t) perm, (scmp_datum_t) perm)) != 0)
            throw SysError("unable to add seccomp rule");

        if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(fchmod), 1,
                SCMP_A1(SCMP_CMP_MASKED_EQ, (scmp_datum_t) perm, (scmp_datum_t) perm)) != 0)
            throw SysError("unable to add seccomp rule");

        if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(fchmodat), 1,
                SCMP_A2(SCMP_CMP_MASKED_EQ, (scmp_datum_t) perm, (scmp_datum_t) perm)) != 0)
            throw SysError("unable to add seccomp rule");
    }

    /* Prevent builders from creating EAs or ACLs. Not all filesystems
       support these, and they're not allowed in the Nix store because
       they're not representable in the NAR serialisation. */
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(setxattr), 0) != 0 ||
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(lsetxattr), 0) != 0 ||
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(fsetxattr), 0) != 0)
        throw SysError("unable to add seccomp rule");

    if (seccomp_attr_set(ctx, SCMP_FLTATR_CTL_NNP, settings.allowNewPrivileges ? 0 : 1) != 0)
        throw SysError("unable to set 'no new privileges' seccomp attribute");

    if (seccomp_load(ctx) != 0)
        throw SysError("unable to load seccomp BPF program");
#else
    throw Error(
        "seccomp is not supported on this platform; "
        "you can bypass this error by setting the option 'filter-syscalls' to false, but note that untrusted builds can then create setuid binaries!");
#endif
#endif
}


void LocalDerivationGoal::runChild()
{
    /* Warning: in the child we should absolutely not make any SQLite
       calls! */

    bool sendException = true;

    try { /* child */

        commonChildInit();

        try {
            setupSeccomp();
        } catch (...) {
            if (buildUser) throw;
        }

        bool setUser = true;

        /* Make the contents of netrc available to builtin:fetchurl
           (which may run under a different uid and/or in a sandbox). */
        std::string netrcData;
        try {
            if (drv->isBuiltin() && drv->builder == "builtin:fetchurl")
                netrcData = readFile(settings.netrcFile);
        } catch (SystemError &) { }

#if __linux__
        if (useChroot) {

            userNamespaceSync.writeSide = -1;

            if (drainFD(userNamespaceSync.readSide.get()) != "1")
                throw Error("user namespace initialisation failed");

            userNamespaceSync.readSide = -1;

            if (privateNetwork) {

                /* Initialise the loopback interface. */
                AutoCloseFD fd(socket(PF_INET, SOCK_DGRAM, IPPROTO_IP));
                if (!fd) throw SysError("cannot open IP socket");

                struct ifreq ifr;
                strcpy(ifr.ifr_name, "lo");
                ifr.ifr_flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING;
                if (ioctl(fd.get(), SIOCSIFFLAGS, &ifr) == -1)
                    throw SysError("cannot set loopback interface flags");
            }

            /* Set the hostname etc. to fixed values. */
            char hostname[] = "localhost";
            if (sethostname(hostname, sizeof(hostname)) == -1)
                throw SysError("cannot set host name");
            char domainname[] = "(none)"; // kernel default
            if (setdomainname(domainname, sizeof(domainname)) == -1)
                throw SysError("cannot set domain name");

            /* Make all filesystems private.  This is necessary
               because subtrees may have been mounted as "shared"
               (MS_SHARED).  (Systemd does this, for instance.)  Even
               though we have a private mount namespace, mounting
               filesystems on top of a shared subtree still propagates
               outside of the namespace.  Making a subtree private is
               local to the namespace, though, so setting MS_PRIVATE
               does not affect the outside world. */
            if (mount(0, "/", 0, MS_PRIVATE | MS_REC, 0) == -1)
                throw SysError("unable to make '/' private");

            /* Bind-mount chroot directory to itself, to treat it as a
               different filesystem from /, as needed for pivot_root. */
            if (mount(chrootRootDir.c_str(), chrootRootDir.c_str(), 0, MS_BIND, 0) == -1)
                throw SysError("unable to bind mount '%1%'", chrootRootDir);

            /* Bind-mount the sandbox's Nix store onto itself so that
               we can mark it as a "shared" subtree, allowing bind
               mounts made in *this* mount namespace to be propagated
               into the child namespace created by the
               unshare(CLONE_NEWNS) call below.

               Marking chrootRootDir as MS_SHARED causes pivot_root()
               to fail with EINVAL. Don't know why. */
            Path chrootStoreDir = chrootRootDir + worker.store.storeDir;

            if (mount(chrootStoreDir.c_str(), chrootStoreDir.c_str(), 0, MS_BIND, 0) == -1)
                throw SysError("unable to bind mount the Nix store", chrootStoreDir);

            if (mount(0, chrootStoreDir.c_str(), 0, MS_SHARED, 0) == -1)
                throw SysError("unable to make '%s' shared", chrootStoreDir);

            /* Set up a nearly empty /dev, unless the user asked to
               bind-mount the host /dev. */
            Strings ss;
            if (pathsInChroot.find("/dev") == pathsInChroot.end()) {
                createDirs(chrootRootDir + "/dev/shm");
                createDirs(chrootRootDir + "/dev/pts");
                ss.push_back("/dev/full");
                if (worker.store.systemFeatures.get().count("kvm") && pathExists("/dev/kvm"))
                    ss.push_back("/dev/kvm");
                ss.push_back("/dev/null");
                ss.push_back("/dev/random");
                ss.push_back("/dev/tty");
                ss.push_back("/dev/urandom");
                ss.push_back("/dev/zero");
                createSymlink("/proc/self/fd", chrootRootDir + "/dev/fd");
                createSymlink("/proc/self/fd/0", chrootRootDir + "/dev/stdin");
                createSymlink("/proc/self/fd/1", chrootRootDir + "/dev/stdout");
                createSymlink("/proc/self/fd/2", chrootRootDir + "/dev/stderr");
            }

            /* Fixed-output derivations typically need to access the
               network, so give them access to /etc/resolv.conf and so
               on. */
            if (!derivationType->isSandboxed()) {
                // Only use nss functions to resolve hosts and
                // services. Don’t use it for anything else that may
                // be configured for this system. This limits the
                // potential impurities introduced in fixed-outputs.
                writeFile(chrootRootDir + "/etc/nsswitch.conf", "hosts: files dns\nservices: files\n");

                /* N.B. it is realistic that these paths might not exist. It
                   happens when testing Nix building fixed-output derivations
                   within a pure derivation. */
                for (auto & path : { "/etc/resolv.conf", "/etc/services", "/etc/hosts" })
                    if (pathExists(path))
                        ss.push_back(path);

                if (settings.caFile != "")
                    pathsInChroot.try_emplace("/etc/ssl/certs/ca-certificates.crt", settings.caFile, true);
            }

            for (auto & i : ss) pathsInChroot.emplace(i, i);

            /* Bind-mount all the directories from the "host"
               filesystem that we want in the chroot
               environment. */
            for (auto & i : pathsInChroot) {
                if (i.second.source == "/proc") continue; // backwards compatibility

                #if HAVE_EMBEDDED_SANDBOX_SHELL
                if (i.second.source == "__embedded_sandbox_shell__") {
                    static unsigned char sh[] = {
                        #include "embedded-sandbox-shell.gen.hh"
                    };
                    auto dst = chrootRootDir + i.first;
                    createDirs(dirOf(dst));
                    writeFile(dst, std::string_view((const char *) sh, sizeof(sh)));
                    chmod_(dst, 0555);
                } else
                #endif
                    doBind(i.second.source, chrootRootDir + i.first, i.second.optional);
            }

            /* Bind a new instance of procfs on /proc. */
            createDirs(chrootRootDir + "/proc");
            if (mount("none", (chrootRootDir + "/proc").c_str(), "proc", 0, 0) == -1)
                throw SysError("mounting /proc");

            /* Mount sysfs on /sys. */
            if (buildUser && buildUser->getUIDCount() != 1) {
                createDirs(chrootRootDir + "/sys");
                if (mount("none", (chrootRootDir + "/sys").c_str(), "sysfs", 0, 0) == -1)
                    throw SysError("mounting /sys");
            }

            /* Mount a new tmpfs on /dev/shm to ensure that whatever
               the builder puts in /dev/shm is cleaned up automatically. */
            if (pathExists("/dev/shm") && mount("none", (chrootRootDir + "/dev/shm").c_str(), "tmpfs", 0,
                    fmt("size=%s", settings.sandboxShmSize).c_str()) == -1)
                throw SysError("mounting /dev/shm");

            /* Mount a new devpts on /dev/pts.  Note that this
               requires the kernel to be compiled with
               CONFIG_DEVPTS_MULTIPLE_INSTANCES=y (which is the case
               if /dev/ptx/ptmx exists). */
            if (pathExists("/dev/pts/ptmx") &&
                !pathExists(chrootRootDir + "/dev/ptmx")
                && !pathsInChroot.count("/dev/pts"))
            {
                if (mount("none", (chrootRootDir + "/dev/pts").c_str(), "devpts", 0, "newinstance,mode=0620") == 0)
                {
                    createSymlink("/dev/pts/ptmx", chrootRootDir + "/dev/ptmx");

                    /* Make sure /dev/pts/ptmx is world-writable.  With some
                       Linux versions, it is created with permissions 0.  */
                    chmod_(chrootRootDir + "/dev/pts/ptmx", 0666);
                } else {
                    if (errno != EINVAL)
                        throw SysError("mounting /dev/pts");
                    doBind("/dev/pts", chrootRootDir + "/dev/pts");
                    doBind("/dev/ptmx", chrootRootDir + "/dev/ptmx");
                }
            }

            /* Make /etc unwritable */
            if (!parsedDrv->useUidRange())
                chmod_(chrootRootDir + "/etc", 0555);

            /* Unshare this mount namespace. This is necessary because
               pivot_root() below changes the root of the mount
               namespace. This means that the call to setns() in
               addDependency() would hide the host's filesystem,
               making it impossible to bind-mount paths from the host
               Nix store into the sandbox. Therefore, we save the
               pre-pivot_root namespace in
               sandboxMountNamespace. Since we made /nix/store a
               shared subtree above, this allows addDependency() to
               make paths appear in the sandbox. */
            if (unshare(CLONE_NEWNS) == -1)
                throw SysError("unsharing mount namespace");

            /* Unshare the cgroup namespace. This means
               /proc/self/cgroup will show the child's cgroup as '/'
               rather than whatever it is in the parent. */
            if (cgroup && unshare(CLONE_NEWCGROUP) == -1)
                throw SysError("unsharing cgroup namespace");

            /* Do the chroot(). */
            if (chdir(chrootRootDir.c_str()) == -1)
                throw SysError("cannot change directory to '%1%'", chrootRootDir);

            if (mkdir("real-root", 0) == -1)
                throw SysError("cannot create real-root directory");

            if (pivot_root(".", "real-root") == -1)
                throw SysError("cannot pivot old root directory onto '%1%'", (chrootRootDir + "/real-root"));

            if (chroot(".") == -1)
                throw SysError("cannot change root directory to '%1%'", chrootRootDir);

            if (umount2("real-root", MNT_DETACH) == -1)
                throw SysError("cannot unmount real root filesystem");

            if (rmdir("real-root") == -1)
                throw SysError("cannot remove real-root directory");

            /* Switch to the sandbox uid/gid in the user namespace,
               which corresponds to the build user or calling user in
               the parent namespace. */
            if (setgid(sandboxGid()) == -1)
                throw SysError("setgid failed");
            if (setuid(sandboxUid()) == -1)
                throw SysError("setuid failed");

            setUser = false;
        }
#endif

        if (chdir(tmpDirInSandbox.c_str()) == -1)
            throw SysError("changing into '%1%'", tmpDir);

        /* Close all other file descriptors. */
        closeMostFDs({STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO});

        setPersonality(drv->platform);

        /* Disable core dumps by default. */
        struct rlimit limit = { 0, RLIM_INFINITY };
        setrlimit(RLIMIT_CORE, &limit);

        // FIXME: set other limits to deterministic values?

        /* Fill in the environment. */
        Strings envStrs;
        for (auto & i : env)
            envStrs.push_back(rewriteStrings(i.first + "=" + i.second, inputRewrites));

        /* If we are running in `build-users' mode, then switch to the
           user we allocated above.  Make sure that we drop all root
           privileges.  Note that above we have closed all file
           descriptors except std*, so that's safe.  Also note that
           setuid() when run as root sets the real, effective and
           saved UIDs. */
        if (setUser && buildUser) {
            /* Preserve supplementary groups of the build user, to allow
               admins to specify groups such as "kvm".  */
            auto gids = buildUser->getSupplementaryGIDs();
            if (setgroups(gids.size(), gids.data()) == -1)
                throw SysError("cannot set supplementary groups of build user");

            if (setgid(buildUser->getGID()) == -1 ||
                getgid() != buildUser->getGID() ||
                getegid() != buildUser->getGID())
                throw SysError("setgid failed");

            if (setuid(buildUser->getUID()) == -1 ||
                getuid() != buildUser->getUID() ||
                geteuid() != buildUser->getUID())
                throw SysError("setuid failed");
        }

        /* Fill in the arguments. */
        Strings args;

        std::string builder = "invalid";

        if (drv->isBuiltin()) {
            ;
        }
#if __APPLE__
        else {
            /* This has to appear before import statements. */
            std::string sandboxProfile = "(version 1)\n";

            if (useChroot) {

                /* Lots and lots and lots of file functions freak out if they can't stat their full ancestry */
                PathSet ancestry;

                /* We build the ancestry before adding all inputPaths to the store because we know they'll
                   all have the same parents (the store), and there might be lots of inputs. This isn't
                   particularly efficient... I doubt it'll be a bottleneck in practice */
                for (auto & i : pathsInChroot) {
                    Path cur = i.first;
                    while (cur.compare("/") != 0) {
                        cur = dirOf(cur);
                        ancestry.insert(cur);
                    }
                }

                /* And we want the store in there regardless of how empty pathsInChroot. We include the innermost
                   path component this time, since it's typically /nix/store and we care about that. */
                Path cur = worker.store.storeDir;
                while (cur.compare("/") != 0) {
                    ancestry.insert(cur);
                    cur = dirOf(cur);
                }

                /* Add all our input paths to the chroot */
                for (auto & i : inputPaths) {
                    auto p = worker.store.printStorePath(i);
                    pathsInChroot[p] = p;
                }

                /* Violations will go to the syslog if you set this. Unfortunately the destination does not appear to be configurable */
                if (settings.darwinLogSandboxViolations) {
                    sandboxProfile += "(deny default)\n";
                } else {
                    sandboxProfile += "(deny default (with no-log))\n";
                }

                sandboxProfile +=
                    #include "sandbox-defaults.sb"
                    ;

                if (!derivationType->isSandboxed())
                    sandboxProfile +=
                        #include "sandbox-network.sb"
                        ;

                /* Add the output paths we'll use at build-time to the chroot */
                sandboxProfile += "(allow file-read* file-write* process-exec\n";
                for (auto & [_, path] : scratchOutputs)
                    sandboxProfile += fmt("\t(subpath \"%s\")\n", worker.store.printStorePath(path));

                sandboxProfile += ")\n";

                /* Our inputs (transitive dependencies and any impurities computed above)

                   without file-write* allowed, access() incorrectly returns EPERM
                 */
                sandboxProfile += "(allow file-read* file-write* process-exec\n";
                for (auto & i : pathsInChroot) {
                    if (i.first != i.second.source)
                        throw Error(
                            "can't map '%1%' to '%2%': mismatched impure paths not supported on Darwin",
                            i.first, i.second.source);

                    std::string path = i.first;
                    auto optSt = maybeLstat(path.c_str());
                    if (!optSt) {
                        if (i.second.optional)
                            continue;
                        throw SysError("getting attributes of required path '%s", path);
                    }
                    if (S_ISDIR(optSt->st_mode))
                        sandboxProfile += fmt("\t(subpath \"%s\")\n", path);
                    else
                        sandboxProfile += fmt("\t(literal \"%s\")\n", path);
                }
                sandboxProfile += ")\n";

                /* Allow file-read* on full directory hierarchy to self. Allows realpath() */
                sandboxProfile += "(allow file-read*\n";
                for (auto & i : ancestry) {
                    sandboxProfile += fmt("\t(literal \"%s\")\n", i);
                }
                sandboxProfile += ")\n";

                sandboxProfile += additionalSandboxProfile;
            } else
                sandboxProfile +=
                    #include "sandbox-minimal.sb"
                    ;

            debug("Generated sandbox profile:");
            debug(sandboxProfile);

            Path sandboxFile = tmpDir + "/.sandbox.sb";

            writeFile(sandboxFile, sandboxProfile);

            bool allowLocalNetworking = parsedDrv->getBoolAttr("__darwinAllowLocalNetworking");

            /* The tmpDir in scope points at the temporary build directory for our derivation. Some packages try different mechanisms
               to find temporary directories, so we want to open up a broader place for them to put their files, if needed. */
            Path globalTmpDir = canonPath(defaultTempDir(), true);

            /* They don't like trailing slashes on subpath directives */
            while (!globalTmpDir.empty() && globalTmpDir.back() == '/')
                globalTmpDir.pop_back();

            if (getEnv("_NIX_TEST_NO_SANDBOX") != "1") {
                builder = "/usr/bin/sandbox-exec";
                args.push_back("sandbox-exec");
                args.push_back("-f");
                args.push_back(sandboxFile);
                args.push_back("-D");
                args.push_back("_GLOBAL_TMP_DIR=" + globalTmpDir);
                if (allowLocalNetworking) {
                    args.push_back("-D");
                    args.push_back(std::string("_ALLOW_LOCAL_NETWORKING=1"));
                }
                args.push_back(drv->builder);
            } else {
                builder = drv->builder;
                args.push_back(std::string(baseNameOf(drv->builder)));
            }
        }
#else
        else {
            builder = drv->builder;
            args.push_back(std::string(baseNameOf(drv->builder)));
        }
#endif

        for (auto & i : drv->args)
            args.push_back(rewriteStrings(i, inputRewrites));

        /* Indicate that we managed to set up the build environment. */
        writeFull(STDERR_FILENO, std::string("\2\n"));

        sendException = false;

        /* Execute the program.  This should not return. */
        if (drv->isBuiltin()) {
            try {
                logger = makeJSONLogger(*logger);

                std::map<std::string, Path> outputs;
                for (auto & e : drv->outputs)
                    outputs.insert_or_assign(e.first,
                        worker.store.printStorePath(scratchOutputs.at(e.first)));

                if (drv->builder == "builtin:fetchurl")
                    builtinFetchurl(*drv, outputs, netrcData);
                else if (drv->builder == "builtin:buildenv")
                    builtinBuildenv(*drv, outputs);
                else if (drv->builder == "builtin:unpack-channel")
                    builtinUnpackChannel(*drv, outputs);
                else
                    throw Error("unsupported builtin builder '%1%'", drv->builder.substr(8));
                _exit(0);
            } catch (std::exception & e) {
                writeFull(STDERR_FILENO, e.what() + std::string("\n"));
                _exit(1);
            }
        }

#if __APPLE__
        posix_spawnattr_t attrp;

        if (posix_spawnattr_init(&attrp))
            throw SysError("failed to initialize builder");

        if (posix_spawnattr_setflags(&attrp, POSIX_SPAWN_SETEXEC))
            throw SysError("failed to initialize builder");

        if (drv->platform == "aarch64-darwin") {
            // Unset kern.curproc_arch_affinity so we can escape Rosetta
            int affinity = 0;
            sysctlbyname("kern.curproc_arch_affinity", NULL, NULL, &affinity, sizeof(affinity));

            cpu_type_t cpu = CPU_TYPE_ARM64;
            posix_spawnattr_setbinpref_np(&attrp, 1, &cpu, NULL);
        } else if (drv->platform == "x86_64-darwin") {
            cpu_type_t cpu = CPU_TYPE_X86_64;
            posix_spawnattr_setbinpref_np(&attrp, 1, &cpu, NULL);
        }

        posix_spawn(NULL, builder.c_str(), NULL, &attrp, stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());
#else
        execve(builder.c_str(), stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());
#endif

        throw SysError("executing '%1%'", drv->builder);

    } catch (Error & e) {
        if (sendException) {
            writeFull(STDERR_FILENO, "\1\n");
            FdSink sink(STDERR_FILENO);
            sink << e;
            sink.flush();
        } else
            std::cerr << e.msg();
        _exit(1);
    }
}


SingleDrvOutputs LocalDerivationGoal::registerOutputs()
{
    /* When using a build hook, the build hook can register the output
       as valid (by doing `nix-store --import').  If so we don't have
       to do anything here.

       We can only early return when the outputs are known a priori. For
       floating content-addressed derivations this isn't the case.
     */
    if (hook)
        return DerivationGoal::registerOutputs();

    std::map<std::string, ValidPathInfo> infos;

    /* Set of inodes seen during calls to canonicalisePathMetaData()
       for this build's outputs.  This needs to be shared between
       outputs to allow hard links between outputs. */
    InodesSeen inodesSeen;

    Path checkSuffix = ".check";

    std::exception_ptr delayedException;

    /* The paths that can be referenced are the input closures, the
       output paths, and any paths that have been built via recursive
       Nix calls. */
    StorePathSet referenceablePaths;
    for (auto & p : inputPaths) referenceablePaths.insert(p);
    for (auto & i : scratchOutputs) referenceablePaths.insert(i.second);
    for (auto & p : addedPaths) referenceablePaths.insert(p);

    /* FIXME `needsHashRewrite` should probably be removed and we get to the
       real reason why we aren't using the chroot dir */
    auto toRealPathChroot = [&](const Path & p) -> Path {
        return useChroot && !needsHashRewrite()
            ? chrootRootDir + p
            : worker.store.toRealPath(p);
    };

    /* Check whether the output paths were created, and make all
       output paths read-only.  Then get the references of each output (that we
       might need to register), so we can topologically sort them. For the ones
       that are most definitely already installed, we just store their final
       name so we can also use it in rewrites. */
    StringSet outputsToSort;
    struct AlreadyRegistered { StorePath path; };
    struct PerhapsNeedToRegister { StorePathSet refs; };
    std::map<std::string, std::variant<AlreadyRegistered, PerhapsNeedToRegister>> outputReferencesIfUnregistered;
    std::map<std::string, struct stat> outputStats;
    for (auto & [outputName, _] : drv->outputs) {
        auto scratchOutput = get(scratchOutputs, outputName);
        if (!scratchOutput)
            throw BuildError(
                "builder for '%s' has no scratch output for '%s'",
                worker.store.printStorePath(drvPath), outputName);
        auto actualPath = toRealPathChroot(worker.store.printStorePath(*scratchOutput));

        outputsToSort.insert(outputName);

        /* Updated wanted info to remove the outputs we definitely don't need to register */
        auto initialOutput = get(initialOutputs, outputName);
        if (!initialOutput)
            throw BuildError(
                "builder for '%s' has no initial output for '%s'",
                worker.store.printStorePath(drvPath), outputName);
        auto & initialInfo = *initialOutput;

        /* Don't register if already valid, and not checking */
        initialInfo.wanted = buildMode == bmCheck
            || !(initialInfo.known && initialInfo.known->isValid());
        if (!initialInfo.wanted) {
            outputReferencesIfUnregistered.insert_or_assign(
                outputName,
                AlreadyRegistered { .path = initialInfo.known->path });
            continue;
        }

        auto optSt = maybeLstat(actualPath.c_str());
        if (!optSt)
            throw BuildError(
                "builder for '%s' failed to produce output path for output '%s' at '%s'",
                worker.store.printStorePath(drvPath), outputName, actualPath);
        struct stat & st = *optSt;

#ifndef __CYGWIN__
        /* Check that the output is not group or world writable, as
           that means that someone else can have interfered with the
           build.  Also, the output should be owned by the build
           user. */
        if ((!S_ISLNK(st.st_mode) && (st.st_mode & (S_IWGRP | S_IWOTH))) ||
            (buildUser && st.st_uid != buildUser->getUID()))
            throw BuildError(
                    "suspicious ownership or permission on '%s' for output '%s'; rejecting this build output",
                    actualPath, outputName);
#endif

        /* Canonicalise first.  This ensures that the path we're
           rewriting doesn't contain a hard link to /etc/shadow or
           something like that. */
        canonicalisePathMetaData(
            actualPath,
            buildUser ? std::optional(buildUser->getUIDRange()) : std::nullopt,
            inodesSeen);

        bool discardReferences = false;
        if (auto structuredAttrs = parsedDrv->getStructuredAttrs()) {
            if (auto udr = get(*structuredAttrs, "unsafeDiscardReferences")) {
                if (auto output = get(*udr, outputName)) {
                    if (!output->is_boolean())
                        throw Error("attribute 'unsafeDiscardReferences.\"%s\"' of derivation '%s' must be a Boolean", outputName, drvPath.to_string());
                    discardReferences = output->get<bool>();
                }
            }
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

        outputReferencesIfUnregistered.insert_or_assign(
            outputName,
            PerhapsNeedToRegister { .refs = references });
        outputStats.insert_or_assign(outputName, std::move(st));
    }

    auto sortedOutputNames = topoSort(outputsToSort,
        {[&](const std::string & name) {
            auto orifu = get(outputReferencesIfUnregistered, name);
            if (!orifu)
                throw BuildError(
                    "no output reference for '%s' in build of '%s'",
                    name, worker.store.printStorePath(drvPath));
            return std::visit(overloaded {
                /* Since we'll use the already installed versions of these, we
                   can treat them as leaves and ignore any references they
                   have. */
                [&](const AlreadyRegistered &) { return StringSet {}; },
                [&](const PerhapsNeedToRegister & refs) {
                    StringSet referencedOutputs;
                    /* FIXME build inverted map up front so no quadratic waste here */
                    for (auto & r : refs.refs)
                        for (auto & [o, p] : scratchOutputs)
                            if (r == p)
                                referencedOutputs.insert(o);
                    return referencedOutputs;
                },
            }, *orifu);
        }},
        {[&](const std::string & path, const std::string & parent) {
            // TODO with more -vvvv also show the temporary paths for manual inspection.
            return BuildError(
                "cycle detected in build of '%s' in the references of output '%s' from output '%s'",
                worker.store.printStorePath(drvPath), path, parent);
        }});

    std::reverse(sortedOutputNames.begin(), sortedOutputNames.end());

    OutputPathMap finalOutputs;

    for (auto & outputName : sortedOutputNames) {
        auto output = get(drv->outputs, outputName);
        auto scratchPath = get(scratchOutputs, outputName);
        assert(output && scratchPath);
        auto actualPath = toRealPathChroot(worker.store.printStorePath(*scratchPath));

        auto finish = [&](StorePath finalStorePath) {
            /* Store the final path */
            finalOutputs.insert_or_assign(outputName, finalStorePath);
            /* The rewrite rule will be used in downstream outputs that refer to
               use. This is why the topological sort is essential to do first
               before this for loop. */
            if (*scratchPath != finalStorePath)
                outputRewrites[std::string { scratchPath->hashPart() }] = std::string { finalStorePath.hashPart() };
        };

        auto orifu = get(outputReferencesIfUnregistered, outputName);
        assert(orifu);

        std::optional<StorePathSet> referencesOpt = std::visit(overloaded {
            [&](const AlreadyRegistered & skippedFinalPath) -> std::optional<StorePathSet> {
                finish(skippedFinalPath.path);
                return std::nullopt;
            },
            [&](const PerhapsNeedToRegister & r) -> std::optional<StorePathSet> {
                return r.refs;
            },
        }, *orifu);

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
            StoreReferences res {
                .self = false,
            };
            for (auto & r : references) {
                auto name = r.name();
                auto origHash = std::string { r.hashPart() };
                if (r == *scratchPath) {
                    res.self = true;
                } else if (auto outputRewrite = get(outputRewrites, origHash)) {
                    std::string newRef = *outputRewrite;
                    newRef += '-';
                    newRef += name;
                    res.others.insert(StorePath { newRef });
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
                    "output path %1% without valid stats info",
                    actualPath);
            if (outputHash.method.getFileIngestionMethod() == FileIngestionMethod::Flat)
            {
                /* The output path should be a regular file without execute permission. */
                if (!S_ISREG(st->st_mode) || (st->st_mode & S_IXUSR) != 0)
                    throw BuildError(
                        "output path '%1%' should be a non-executable regular file "
                        "since recursive hashing is not enabled (one of outputHashMode={flat,text} is true)",
                        actualPath);
            }
            rewriteOutput(outputRewrites);
            /* FIXME optimize and deduplicate with addToStore */
            std::string oldHashPart { scratchPath->hashPart() };
            auto got = [&]{
                PosixSourceAccessor accessor;
                auto fim = outputHash.method.getFileIngestionMethod();
                switch (fim) {
                case FileIngestionMethod::Flat:
                case FileIngestionMethod::Recursive:
                {
                    HashModuloSink caSink { outputHash.hashAlgo, oldHashPart };
                    auto fim = outputHash.method.getFileIngestionMethod();
                    dumpPath(
                        accessor, CanonPath { actualPath },
                        caSink,
                        (FileSerialisationMethod) fim);
                    return caSink.finish().first;
                }
                case FileIngestionMethod::Git: {
                    return git::dumpHash(
                        outputHash.hashAlgo, accessor,
                        CanonPath { tmpDir + "/tmp" }).hash;
                }
                }
                assert(false);
            }();

            ValidPathInfo newInfo0 {
                worker.store,
                outputPathName(drv->name, outputName),
                ContentAddressWithReferences::fromParts(
                    outputHash.method,
                    std::move(got),
                    rewriteRefs()),
                Hash::dummy,
            };
            if (*scratchPath != newInfo0.path) {
                // If the path has some self-references, we need to rewrite
                // them.
                // (note that this doesn't invalidate the ca hash we calculated
                // above because it's computed *modulo the self-references*, so
                // it already takes this rewrite into account).
                rewriteOutput(
                    StringMap{{oldHashPart,
                               std::string(newInfo0.path.hashPart())}});
            }

            {
                PosixSourceAccessor accessor;
                HashResult narHashAndSize = hashPath(
                    accessor, CanonPath { actualPath },
                    FileSerialisationMethod::Recursive, HashAlgorithm::SHA256);
                newInfo0.narHash = narHashAndSize.first;
                newInfo0.narSize = narHashAndSize.second;
            }

            assert(newInfo0.ca);
            return newInfo0;
        };

        ValidPathInfo newInfo = std::visit(overloaded {

            [&](const DerivationOutput::InputAddressed & output) {
                /* input-addressed case */
                auto requiredFinalPath = output.path;
                /* Preemptively add rewrite rule for final hash, as that is
                   what the NAR hash will use rather than normalized-self references */
                if (*scratchPath != requiredFinalPath)
                    outputRewrites.insert_or_assign(
                        std::string { scratchPath->hashPart() },
                        std::string { requiredFinalPath.hashPart() });
                rewriteOutput(outputRewrites);
                PosixSourceAccessor accessor;
                HashResult narHashAndSize = hashPath(
                    accessor, CanonPath { actualPath },
                    FileSerialisationMethod::Recursive, HashAlgorithm::SHA256);
                ValidPathInfo newInfo0 { requiredFinalPath, narHashAndSize.first };
                newInfo0.narSize = narHashAndSize.second;
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
                copyFile(actualPath, tmpOutput, true);
                renameFile(tmpOutput, actualPath);

                auto newInfo0 = newInfoFromCA(DerivationOutput::CAFloating {
                    .method = dof.ca.method,
                    .hashAlgo = wanted.algo,
                });

                /* Check wanted hash */
                assert(newInfo0.ca);
                auto & got = newInfo0.ca->hash;
                if (wanted != got) {
                    /* Throw an error after registering the path as
                       valid. */
                    worker.hashMismatch = true;
                    delayedException = std::make_exception_ptr(
                        BuildError("hash mismatch in fixed-output derivation '%s':\n  specified: %s\n     got:    %s",
                            worker.store.printStorePath(drvPath),
                            wanted.to_string(HashFormat::SRI, true),
                            got.to_string(HashFormat::SRI, true)));
                }
                if (!newInfo0.references.empty())
                    delayedException = std::make_exception_ptr(
                        BuildError("illegal path references in fixed-output derivation '%s'",
                            worker.store.printStorePath(drvPath)));

                return newInfo0;
            },

            [&](const DerivationOutput::CAFloating & dof) {
                return newInfoFromCA(dof);
            },

            [&](const DerivationOutput::Deferred &) -> ValidPathInfo {
                // No derivation should reach that point without having been
                // rewritten first
                assert(false);
            },

            [&](const DerivationOutput::Impure & doi) {
                return newInfoFromCA(DerivationOutput::CAFloating {
                    .method = doi.method,
                    .hashAlgo = doi.hashAlgo,
                });
            },

        }, output->raw);

        /* FIXME: set proper permissions in restorePath() so
            we don't have to do another traversal. */
        canonicalisePathMetaData(actualPath, {}, inodesSeen);

        /* Calculate where we'll move the output files. In the checking case we
           will leave leave them where they are, for now, rather than move to
           their usual "final destination" */
        auto finalDestPath = worker.store.printStorePath(newInfo.path);

        /* Lock final output path, if not already locked. This happens with
           floating CA derivations and hash-mismatching fixed-output
           derivations. */
        PathLocks dynamicOutputLock;
        dynamicOutputLock.setDeletion(true);
        auto optFixedPath = output->path(worker.store, drv->name, outputName);
        if (!optFixedPath ||
            worker.store.printStorePath(*optFixedPath) != finalDestPath)
        {
            assert(newInfo.ca);
            dynamicOutputLock.lockPaths({worker.store.toRealPath(finalDestPath)});
        }

        /* Move files, if needed */
        if (worker.store.toRealPath(finalDestPath) != actualPath) {
            if (buildMode == bmRepair) {
                /* Path already exists, need to replace it */
                replaceValidPath(worker.store.toRealPath(finalDestPath), actualPath);
                actualPath = worker.store.toRealPath(finalDestPath);
            } else if (buildMode == bmCheck) {
                /* Path already exists, and we want to compare, so we leave out
                   new path in place. */
            } else if (worker.store.isValidPath(newInfo.path)) {
                /* Path already exists because CA path produced by something
                   else. No moving needed. */
                assert(newInfo.ca);
            } else {
                auto destPath = worker.store.toRealPath(finalDestPath);
                deletePath(destPath);
                movePath(actualPath, destPath);
                actualPath = destPath;
            }
        }

        auto & localStore = getLocalStore();

        if (buildMode == bmCheck) {

            if (!worker.store.isValidPath(newInfo.path)) continue;
            ValidPathInfo oldInfo(*worker.store.queryPathInfo(newInfo.path));
            if (newInfo.narHash != oldInfo.narHash) {
                worker.checkMismatch = true;
                if (settings.runDiffHook || settings.keepFailed) {
                    auto dst = worker.store.toRealPath(finalDestPath + checkSuffix);
                    deletePath(dst);
                    movePath(actualPath, dst);

                    handleDiffHook(
                        buildUser ? buildUser->getUID() : getuid(),
                        buildUser ? buildUser->getGID() : getgid(),
                        finalDestPath, dst, worker.store.printStorePath(drvPath), tmpDir);

                    throw NotDeterministic("derivation '%s' may not be deterministic: output '%s' differs from '%s'",
                        worker.store.printStorePath(drvPath), worker.store.toRealPath(finalDestPath), dst);
                } else
                    throw NotDeterministic("derivation '%s' may not be deterministic: output '%s' differs",
                        worker.store.printStorePath(drvPath), worker.store.toRealPath(finalDestPath));
            }

            /* Since we verified the build, it's now ultimately trusted. */
            if (!oldInfo.ultimate) {
                oldInfo.ultimate = true;
                localStore.signPathInfo(oldInfo);
                localStore.registerValidPaths({{oldInfo.path, oldInfo}});
            }

            continue;
        }

        /* For debugging, print out the referenced and unreferenced paths. */
        for (auto & i : inputPaths) {
            if (references.count(i))
                debug("referenced input: '%1%'", worker.store.printStorePath(i));
            else
                debug("unreferenced input: '%1%'", worker.store.printStorePath(i));
        }

        localStore.optimisePath(actualPath, NoRepair); // FIXME: combine with scanForReferences()
        worker.markContentsGood(newInfo.path);

        newInfo.deriver = drvPath;
        newInfo.ultimate = true;
        localStore.signPathInfo(newInfo);

        finish(newInfo.path);

        /* If it's a CA path, register it right away. This is necessary if it
           isn't statically known so that we can safely unlock the path before
           the next iteration */
        if (newInfo.ca)
            localStore.registerValidPaths({{newInfo.path, newInfo}});

        infos.emplace(outputName, std::move(newInfo));
    }

    if (buildMode == bmCheck) {
        /* In case of fixed-output derivations, if there are
           mismatches on `--check` an error must be thrown as this is
           also a source for non-determinism. */
        if (delayedException)
            std::rethrow_exception(delayedException);
        return assertPathValidity();
    }

    /* Apply output checks. */
    checkOutputs(infos);

    /* Register each output path as valid, and register the sets of
       paths referenced by each of them.  If there are cycles in the
       outputs, this will fail. */
    {
        auto & localStore = getLocalStore();

        ValidPathInfos infos2;
        for (auto & [outputName, newInfo] : infos) {
            infos2.insert_or_assign(newInfo.path, newInfo);
        }
        localStore.registerValidPaths(infos2);
    }

    /* In case of a fixed-output derivation hash mismatch, throw an
       exception now that we have registered the output as valid. */
    if (delayedException)
        std::rethrow_exception(delayedException);

    /* If we made it this far, we are sure the output matches the derivation
       (since the delayedException would be a fixed output CA mismatch). That
       means it's safe to link the derivation to the output hash. We must do
       that for floating CA derivations, which otherwise couldn't be cached,
       but it's fine to do in all cases. */
    SingleDrvOutputs builtOutputs;

    for (auto & [outputName, newInfo] : infos) {
        auto oldinfo = get(initialOutputs, outputName);
        assert(oldinfo);
        auto thisRealisation = Realisation {
            .id = DrvOutput {
                oldinfo->outputHash,
                outputName
            },
            .outPath = newInfo.path
        };
        if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)
            && !drv->type().isImpure())
        {
            signRealisation(thisRealisation);
            worker.store.registerDrvOutput(thisRealisation);
        }
        builtOutputs.emplace(outputName, thisRealisation);
    }

    return builtOutputs;
}

void LocalDerivationGoal::signRealisation(Realisation & realisation)
{
    getLocalStore().signRealisation(realisation);
}


void LocalDerivationGoal::checkOutputs(const std::map<std::string, ValidPathInfo> & outputs)
{
    std::map<Path, const ValidPathInfo &> outputsByPath;
    for (auto & output : outputs)
        outputsByPath.emplace(worker.store.printStorePath(output.second.path), output.second);

    for (auto & output : outputs) {
        auto & outputName = output.first;
        auto & info = output.second;

        struct Checks
        {
            bool ignoreSelfRefs = false;
            std::optional<uint64_t> maxSize, maxClosureSize;
            std::optional<Strings> allowedReferences, allowedRequisites, disallowedReferences, disallowedRequisites;
        };

        /* Compute the closure and closure size of some output. This
           is slightly tricky because some of its references (namely
           other outputs) may not be valid yet. */
        auto getClosure = [&](const StorePath & path)
        {
            uint64_t closureSize = 0;
            StorePathSet pathsDone;
            std::queue<StorePath> pathsLeft;
            pathsLeft.push(path);

            while (!pathsLeft.empty()) {
                auto path = pathsLeft.front();
                pathsLeft.pop();
                if (!pathsDone.insert(path).second) continue;

                auto i = outputsByPath.find(worker.store.printStorePath(path));
                if (i != outputsByPath.end()) {
                    closureSize += i->second.narSize;
                    for (auto & ref : i->second.references)
                        pathsLeft.push(ref);
                } else {
                    auto info = worker.store.queryPathInfo(path);
                    closureSize += info->narSize;
                    for (auto & ref : info->references)
                        pathsLeft.push(ref);
                }
            }

            return std::make_pair(std::move(pathsDone), closureSize);
        };

        auto applyChecks = [&](const Checks & checks)
        {
            if (checks.maxSize && info.narSize > *checks.maxSize)
                throw BuildError("path '%s' is too large at %d bytes; limit is %d bytes",
                    worker.store.printStorePath(info.path), info.narSize, *checks.maxSize);

            if (checks.maxClosureSize) {
                uint64_t closureSize = getClosure(info.path).second;
                if (closureSize > *checks.maxClosureSize)
                    throw BuildError("closure of path '%s' is too large at %d bytes; limit is %d bytes",
                        worker.store.printStorePath(info.path), closureSize, *checks.maxClosureSize);
            }

            auto checkRefs = [&](const std::optional<Strings> & value, bool allowed, bool recursive)
            {
                if (!value) return;

                /* Parse a list of reference specifiers.  Each element must
                   either be a store path, or the symbolic name of the output
                   of the derivation (such as `out'). */
                StorePathSet spec;
                for (auto & i : *value) {
                    if (worker.store.isStorePath(i))
                        spec.insert(worker.store.parseStorePath(i));
                    else if (auto output = get(outputs, i))
                        spec.insert(output->path);
                    else
                        throw BuildError("derivation contains an illegal reference specifier '%s'", i);
                }

                auto used = recursive
                    ? getClosure(info.path).first
                    : info.references;

                if (recursive && checks.ignoreSelfRefs)
                    used.erase(info.path);

                StorePathSet badPaths;

                for (auto & i : used)
                    if (allowed) {
                        if (!spec.count(i))
                            badPaths.insert(i);
                    } else {
                        if (spec.count(i))
                            badPaths.insert(i);
                    }

                if (!badPaths.empty()) {
                    std::string badPathsStr;
                    for (auto & i : badPaths) {
                        badPathsStr += "\n  ";
                        badPathsStr += worker.store.printStorePath(i);
                    }
                    throw BuildError("output '%s' is not allowed to refer to the following paths:%s",
                        worker.store.printStorePath(info.path), badPathsStr);
                }
            };

            checkRefs(checks.allowedReferences, true, false);
            checkRefs(checks.allowedRequisites, true, true);
            checkRefs(checks.disallowedReferences, false, false);
            checkRefs(checks.disallowedRequisites, false, true);
        };

        if (auto structuredAttrs = parsedDrv->getStructuredAttrs()) {
            if (auto outputChecks = get(*structuredAttrs, "outputChecks")) {
                if (auto output = get(*outputChecks, outputName)) {
                    Checks checks;

                    if (auto maxSize = get(*output, "maxSize"))
                        checks.maxSize = maxSize->get<uint64_t>();

                    if (auto maxClosureSize = get(*output, "maxClosureSize"))
                        checks.maxClosureSize = maxClosureSize->get<uint64_t>();

                    auto get_ = [&](const std::string & name) -> std::optional<Strings> {
                        if (auto i = get(*output, name)) {
                            Strings res;
                            for (auto j = i->begin(); j != i->end(); ++j) {
                                if (!j->is_string())
                                    throw Error("attribute '%s' of derivation '%s' must be a list of strings", name, worker.store.printStorePath(drvPath));
                                res.push_back(j->get<std::string>());
                            }
                            checks.disallowedRequisites = res;
                            return res;
                        }
                        return {};
                    };

                    checks.allowedReferences = get_("allowedReferences");
                    checks.allowedRequisites = get_("allowedRequisites");
                    checks.disallowedReferences = get_("disallowedReferences");
                    checks.disallowedRequisites = get_("disallowedRequisites");

                    applyChecks(checks);
                }
            }
        } else {
            // legacy non-structured-attributes case
            Checks checks;
            checks.ignoreSelfRefs = true;
            checks.allowedReferences = parsedDrv->getStringsAttr("allowedReferences");
            checks.allowedRequisites = parsedDrv->getStringsAttr("allowedRequisites");
            checks.disallowedReferences = parsedDrv->getStringsAttr("disallowedReferences");
            checks.disallowedRequisites = parsedDrv->getStringsAttr("disallowedRequisites");
            applyChecks(checks);
        }
    }
}


void LocalDerivationGoal::deleteTmpDir(bool force)
{
    if (tmpDir != "") {
        /* Don't keep temporary directories for builtins because they
           might have privileged stuff (like a copy of netrc). */
        if (settings.keepFailed && !force && !drv->isBuiltin()) {
            printError("note: keeping build directory '%s'", tmpDir);
            chmod(tmpDir.c_str(), 0755);
        }
        else
            deletePath(tmpDir);
        tmpDir = "";
    }
}


bool LocalDerivationGoal::isReadDesc(int fd)
{
    return (hook && DerivationGoal::isReadDesc(fd)) ||
        (!hook && fd == builderOut.get());
}


StorePath LocalDerivationGoal::makeFallbackPath(OutputNameView outputName)
{
    return worker.store.makeStorePath(
        "rewrite:" + std::string(drvPath.to_string()) + ":name:" + std::string(outputName),
        Hash(HashAlgorithm::SHA256), outputPathName(drv->name, outputName));
}


StorePath LocalDerivationGoal::makeFallbackPath(const StorePath & path)
{
    return worker.store.makeStorePath(
        "rewrite:" + std::string(drvPath.to_string()) + ":" + std::string(path.to_string()),
        Hash(HashAlgorithm::SHA256), path.name());
}


}
