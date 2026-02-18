#include "linux-chroot-derivation-builder.hh"
#include "linux-derivation-builder-common.hh"
#include "derivation-builder-common.hh"
#include "nix/store/build/derivation-builder.hh"
#include "nix/util/file-system.hh"
#include "nix/store/local-store.hh"
#include "nix/util/processes.hh"
#include "nix/store/builtins.hh"
#include "nix/store/build/child.hh"
#include "nix/store/restricted-store.hh"
#include "nix/store/user-lock.hh"
#include "nix/store/globals.hh"
#include "nix/store/personality.hh"
#include "nix/util/linux-namespaces.hh"
#include "nix/store/filetransfer.hh"
#include "store-config-private.hh"
#include "chroot.hh"

#include <fcntl.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/syscall.h>

#include <grp.h>

#include "nix/util/strings.hh"
#include "nix/util/signals.hh"
#include "nix/util/cgroup.hh"

#if NIX_WITH_AWS_AUTH
#  include "nix/store/aws-creds.hh"
#endif

#define pivot_root(new_root, put_old) (syscall(SYS_pivot_root, new_root, put_old))

namespace nix {

using namespace nix::linux;

static void doBind(const std::filesystem::path & source, const std::filesystem::path & target, bool optional = false)
{
    debug("bind mounting %1% to %2%", PathFmt(source), PathFmt(target));

    auto bindMount = [&]() {
        if (mount(source.c_str(), target.c_str(), "", MS_BIND | MS_REC, 0) == -1)
            throw SysError("bind mount from %1% to %2% failed", PathFmt(source), PathFmt(target));
    };

    auto maybeSt = maybeLstat(source);
    if (!maybeSt) {
        if (optional)
            return;
        else
            throw SysError("getting attributes of path %1%", PathFmt(source));
    }
    auto st = *maybeSt;

    if (S_ISDIR(st.st_mode)) {
        createDirs(target);
        bindMount();
    } else if (S_ISLNK(st.st_mode)) {
        // Symlinks can (apparently) not be bind-mounted, so just copy it
        createDirs(target.parent_path());
        copyFile(source, target, false);
    } else {
        createDirs(target.parent_path());
        writeFile(target, "");
        bindMount();
    }
}

static const std::filesystem::path procPath = "/proc";

struct LinuxChrootDerivationBuilder : DerivationBuilder, DerivationBuilderParams
{
    Pid pid;

    LocalStore & store;

    const LocalSettings & localSettings = store.config->getLocalSettings();

    std::unique_ptr<DerivationBuilderCallbacks> miscMethods;

    std::unique_ptr<UserLock> buildUser;

    std::filesystem::path tmpDir;

    std::filesystem::path topTmpDir;

    const DerivationType derivationType;

    StringMap env;

    std::map<StorePath, StorePath> redirectedOutputs;

    OutputPathMap scratchOutputs;

    AutoCloseFD tmpDirFd;

    StringMap inputRewrites, outputRewrites;

    static const std::filesystem::path homeDir;

    AutoCloseFD daemonSocket;
    std::thread daemonThread;
    std::vector<std::thread> daemonWorkerThreads;

    std::filesystem::path chrootRootDir;

    std::optional<AutoDelete> autoDelChroot;

    PathsInChroot pathsInChroot;

    Pipe userNamespaceSync;

    AutoCloseFD sandboxMountNamespace;
    AutoCloseFD sandboxUserNamespace;

    bool usingUserNamespace = true;

    std::optional<std::filesystem::path> cgroup;

    LinuxChrootDerivationBuilder(
        LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
        : DerivationBuilderParams{std::move(params)}
        , store{store}
        , miscMethods{std::move(miscMethods)}
        , derivationType{drv.type()}
    {
    }

    void cleanupOnDestruction() noexcept override
    {
        try {
            killChild();
        } catch (...) {
            ignoreExceptionInDestructor();
        }
        try {
            nix::stopDaemon(daemonSocket, daemonThread, daemonWorkerThreads);
        } catch (...) {
            ignoreExceptionInDestructor();
        }
        try {
            cleanupBuild(false);
        } catch (...) {
            ignoreExceptionInDestructor();
        }
    }

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

    friend struct RestrictedStore;

    bool needsHashRewrite()
    {
        return false;
    }

    uid_t sandboxUid()
    {
        return usingUserNamespace ? (!buildUser || buildUser->getUIDCount() == 1 ? 1000 : 0) : buildUser->getUID();
    }

    gid_t sandboxGid()
    {
        return usingUserNamespace ? (!buildUser || buildUser->getUIDCount() == 1 ? 100 : 0) : buildUser->getGID();
    }

    std::filesystem::path tmpDirInSandbox()
    {
        return store.config->getLocalSettings().sandboxBuildDir.get();
    }

    void addDependencyImpl(const StorePath & path) override
    {
        addedPaths.insert(path);

        debug("materialising '%s' in the sandbox", store.printStorePath(path));

        std::filesystem::path source = store.toRealPath(path);
        std::filesystem::path target =
            chrootRootDir / std::filesystem::path(store.printStorePath(path)).relative_path();

        if (pathExists(target)) {
            debug("bind-mounting %s -> %s", PathFmt(target), PathFmt(source));
            throw Error("store path '%s' already exists in the sandbox", store.printStorePath(path));
        }

        Pid child(startProcess([&]() {
            if (usingUserNamespace && (setns(sandboxUserNamespace.get(), CLONE_NEWUSER) == -1))
                throw SysError("entering sandbox user namespace");

            if (setns(sandboxMountNamespace.get(), CLONE_NEWNS) == -1)
                throw SysError("entering sandbox mount namespace");

            doBind(source, target);

            _exit(0);
        }));

        int status = child.wait();
        if (status != 0)
            throw Error("could not add path '%s' to sandbox", store.printStorePath(path));
    }

    void killSandbox(bool getStats)
    {
        if (cgroup) {
            auto stats = destroyCgroup(*cgroup);
            if (getStats) {
                buildResult.cpuUser = stats.cpuUser;
                buildResult.cpuSystem = stats.cpuSystem;
            }
            return;
        }

        if (buildUser) {
            auto uid = buildUser->getUID();
            assert(uid != 0);
            killUser(uid);
        }
    }

    bool killChild() override
    {
        bool ret = pid != -1;
        if (ret) {
            ::kill(-pid, SIGKILL);
            killSandbox(true);
            pid.wait();
            miscMethods->childTerminated();
        }
        return ret;
    }

    void cleanupBuild(bool force)
    {
        nix::cleanupBuildCore(force, store, redirectedOutputs, drv, topTmpDir, tmpDir);

        if (autoDelChroot) {
            /* Move paths out of the chroot for easier debugging of
               build failures. */
            if (!force && buildMode == bmNormal)
                for (auto & [_, status] : initialOutputs) {
                    if (!status.known)
                        continue;
                    if (buildMode != bmCheck && status.known->isValid())
                        continue;
                    std::filesystem::path p = store.toRealPath(status.known->path);
                    std::filesystem::path chrootPath = chrootRootDir / p.relative_path();
                    if (pathExists(chrootPath))
                        std::filesystem::rename(chrootPath, p);
                }

            autoDelChroot.reset();
        }
    }

    std::optional<Descriptor> startBuild() override
    {
        if (useBuildUsers(localSettings)) {
            if (!buildUser)
                buildUser = acquireUserLock(
                    settings.nixStateDir,
                    store.config->getLocalSettings(),
                    drvOptions.useUidRange(drv) ? 65536 : 1,
                    true);

            if (!buildUser)
                return std::nullopt;
        }

        /* Prepare cgroup and kill any previous sandbox */
        if ((buildUser && buildUser->getUIDCount() != 1) || store.config->getLocalSettings().useCgroups) {
            experimentalFeatureSettings.require(Xp::Cgroups);

            auto cgroupFS = getCgroupFS();
            if (!cgroupFS)
                throw Error("cannot determine the cgroups file system");
            auto rootCgroupPath = *cgroupFS / getRootCgroup().rel();
            if (!pathExists(rootCgroupPath))
                throw Error("expected cgroup directory %s", PathFmt(rootCgroupPath));

            static std::atomic<unsigned int> counter{0};

            cgroup = rootCgroupPath
                     / (buildUser ? fmt("nix-build-uid-%d", buildUser->getUID())
                                  : fmt("nix-build-pid-%d-%d", getpid(), counter++));

            debug("using cgroup %s", PathFmt(*cgroup));

            if (buildUser) {
                auto cgroupsDir = std::filesystem::path{settings.nixStateDir} / "cgroups";
                createDirs(cgroupsDir);

                auto cgroupFile = cgroupsDir / std::to_string(buildUser->getUID());

                if (pathExists(cgroupFile)) {
                    auto prevCgroup = readFile(cgroupFile);
                    destroyCgroup(prevCgroup);
                }

                writeFile(cgroupFile, cgroup->native());
            }
        }

        killSandbox(false);

        auto buildDir = store.config->getBuildDir();

        createDirs(buildDir);

        if (buildUser)
            checkNotWorldWritable(buildDir);

        topTmpDir = createTempDir(buildDir, "nix", 0700);
        tmpDir = topTmpDir / "build";
        createDir(tmpDir, 0700);
        assert(!tmpDir.empty());

        tmpDirFd = AutoCloseFD{open(tmpDir.c_str(), O_RDONLY | O_NOFOLLOW | O_DIRECTORY)};
        if (!tmpDirFd)
            throw SysError("failed to open the build temporary directory descriptor %1%", PathFmt(tmpDir));

        nix::chownToBuilder(buildUser.get(), tmpDirFd.get(), tmpDir);

        inputRewrites.clear();
        nix::computeScratchOutputs(store, *this, scratchOutputs, redirectedOutputs, inputRewrites, needsHashRewrite());

        nix::initEnv(
            env,
            homeDir,
            store.storeDir,
            *this,
            inputRewrites,
            derivationType,
            localSettings,
            tmpDirInSandbox(),
            buildUser.get(),
            tmpDir,
            tmpDirFd.get());

        // Start with the default sandbox paths
        pathsInChroot = defaultPathsInChroot;

        if (hasPrefix(store.storeDir, tmpDirInSandbox().native())) {
            throw Error("`sandbox-build-dir` must not contain the storeDir");
        }
        pathsInChroot[tmpDirInSandbox()] = {.source = tmpDir};

        nix::checkAndAddImpurePaths(
            pathsInChroot, drvOptions, store, drvPath, localSettings.allowedImpureHostPrefixes);

        for (auto & i : inputPaths) {
            auto p = store.printStorePath(i);
            pathsInChroot.insert_or_assign(p, ChrootPath{.source = store.toRealPath(p)});
        }

        /* If we're repairing, checking or rebuilding part of a
           multiple-outputs derivation, it's possible that we're
           rebuilding a path that is in settings.sandbox-paths
           (typically the dependencies of /bin/sh).  Throw them
           out. */
        for (auto & i : drv.outputsAndOptPaths(store)) {
            if (i.second.second)
                pathsInChroot.erase(store.printStorePath(*i.second.second));
        }

        // Set up chroot parameters
        BuildChrootParams chrootParams{
            .chrootParentDir = store.toRealPath(drvPath) + ".chroot",
            .useUidRange = drvOptions.useUidRange(drv),
            .isSandboxed = derivationType.isSandboxed(),
            .buildUser = buildUser.get(),
            .storeDir = store.storeDir,
            .chownToBuilder = [this](const std::filesystem::path & path) { nix::chownToBuilder(buildUser.get(), path); },
            .getSandboxGid = [this]() { return this->sandboxGid(); },
        };

        auto [rootDir, cleanup] = setupBuildChroot(chrootParams);
        chrootRootDir = std::move(rootDir);
        autoDelChroot.emplace(std::move(cleanup));

        if (localSettings.preBuildHook != "") {
            printMsg(lvlChatty, "executing pre-build hook '%1%'", localSettings.preBuildHook);
            assert(!chrootRootDir.empty());
            auto lines = runProgram(
                localSettings.preBuildHook, false, Strings({store.printStorePath(drvPath), chrootRootDir.native()}));
            nix::parsePreBuildHook(pathsInChroot, lines);
        }

        if (cgroup) {
            if (mkdir(cgroup->c_str(), 0755) != 0)
                throw SysError("creating cgroup %s", PathFmt(*cgroup));
            nix::chownToBuilder(buildUser.get(), *cgroup);
            nix::chownToBuilder(buildUser.get(), *cgroup / "cgroup.procs");
            nix::chownToBuilder(buildUser.get(), *cgroup / "cgroup.threads");
        }

        if (needsHashRewrite() && pathExists(homeDir))
            throw Error(
                "home directory %1% exists; please remove it to assure purity of builds without sandboxing",
                PathFmt(homeDir));

        if (drvOptions.getRequiredSystemFeatures(drv).count("recursive-nix"))
            nix::setupRecursiveNixDaemon(
                store, *this, *this, addedPaths, env, tmpDir, tmpDirInSandbox(),
                daemonSocket, daemonThread, daemonWorkerThreads, buildUser.get());

        nix::logBuilderInfo(drv);

        miscMethods->openLogFile();

        nix::setupPTYMaster(builderOut, buildUser.get());

        buildResult.startTime = time(0);

        // startChild() inlined
        {
#if NIX_WITH_AWS_AUTH
            auto awsCredentials = nix::preResolveAwsCredentials(drv);
#endif

            userNamespaceSync.create();

            usingUserNamespace = userNamespacesSupported();

            Pipe sendPid;
            sendPid.create();

            Pid helper = startProcess([&]() {
                sendPid.readSide.close();

                /* We need to open the slave early, before
                   CLONE_NEWUSER. Otherwise we get EPERM when running as
                   root. */
                nix::setupPTYSlave(builderOut.get());

                try {
                    /* Drop additional groups here because we can't do it
                       after we've created the new user namespace. */
                    if (setgroups(0, 0) == -1) {
                        if (errno != EPERM)
                            throw SysError("setgroups failed");
                        if (store.config->getLocalSettings().requireDropSupplementaryGroups)
                            throw Error(
                                "setgroups failed. Set the require-drop-supplementary-groups option to false to skip this step.");
                    }

                    ProcessOptions options;
                    options.cloneFlags =
                        CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS | CLONE_PARENT | SIGCHLD;
                    if (derivationType.isSandboxed())
                        options.cloneFlags |= CLONE_NEWNET;
                    if (usingUserNamespace)
                        options.cloneFlags |= CLONE_NEWUSER;

                    pid_t child = startProcess(
                        [this
#if NIX_WITH_AWS_AUTH
                         ,
                         awsCredentials
#endif
                    ]() {
                            // runChild inlined
                            bool sendException = true;

                            try {
                                commonChildInit();

                                BuiltinBuilderContext ctx{
                                    .drv = drv,
                                    .hashedMirrors = settings.getLocalSettings().hashedMirrors,
                                    .tmpDirInSandbox = tmpDirInSandbox(),
#if NIX_WITH_AWS_AUTH
                                    .awsCredentials = awsCredentials,
#endif
                                };

                                nix::setupBuiltinFetchurlContext(ctx, drv);

                                // enterChroot inlined
                                {
                                    userNamespaceSync.writeSide = -1;

                                    if (readLine(userNamespaceSync.readSide.get()) != "1")
                                        throw Error("user namespace initialisation failed");

                                    userNamespaceSync.readSide = -1;

                                    if (derivationType.isSandboxed()) {

                                        /* Initialise the loopback interface. */
                                        AutoCloseFD fd(socket(PF_INET, SOCK_DGRAM, IPPROTO_IP));
                                        if (!fd)
                                            throw SysError("cannot open IP socket");

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

                                    /* Make all filesystems private. */
                                    if (mount(0, "/", 0, MS_PRIVATE | MS_REC, 0) == -1)
                                        throw SysError("unable to make '/' private");

                                    /* Bind-mount chroot directory to itself. */
                                    if (mount(chrootRootDir.c_str(), chrootRootDir.c_str(), 0, MS_BIND, 0) == -1)
                                        throw SysError("unable to bind mount %1%", PathFmt(chrootRootDir));

                                    std::filesystem::path chrootStoreDir =
                                        chrootRootDir / std::filesystem::path(store.storeDir).relative_path();

                                    if (mount(chrootStoreDir.c_str(), chrootStoreDir.c_str(), 0, MS_BIND, 0) == -1)
                                        throw SysError(
                                            "unable to bind mount the Nix store at %1%", PathFmt(chrootStoreDir));

                                    if (mount(0, chrootStoreDir.c_str(), 0, MS_SHARED, 0) == -1)
                                        throw SysError("unable to make %s shared", PathFmt(chrootStoreDir));

                                    Strings ss;
                                    if (pathsInChroot.find("/dev") == pathsInChroot.end()) {
                                        createDirs(chrootRootDir / "dev" / "shm");
                                        createDirs(chrootRootDir / "dev" / "pts");
                                        ss.push_back("/dev/full");
                                        if (systemFeatures.count("kvm")) {
                                            if (pathExists("/dev/kvm")) {
                                                ss.push_back("/dev/kvm");
                                            } else {
                                                warn(
                                                    "KVM is enabled in system-features but /dev/kvm is not available. "
                                                    "QEMU builds may fall back to slow emulation. "
                                                    "Consider removing 'kvm' from system-features in nix.conf if KVM is not supported on this system.");
                                            }
                                        }
                                        ss.push_back("/dev/null");
                                        ss.push_back("/dev/random");
                                        ss.push_back("/dev/tty");
                                        ss.push_back("/dev/urandom");
                                        ss.push_back("/dev/zero");
                                        createSymlink("/proc/self/fd", chrootRootDir / "dev" / "fd");
                                        createSymlink("/proc/self/fd/0", chrootRootDir / "dev" / "stdin");
                                        createSymlink("/proc/self/fd/1", chrootRootDir / "dev" / "stdout");
                                        createSymlink("/proc/self/fd/2", chrootRootDir / "dev" / "stderr");
                                    }

                                    if (!derivationType.isSandboxed()) {
                                        writeFile(
                                            chrootRootDir / "etc" / "nsswitch.conf",
                                            "hosts: files dns\nservices: files\n");

                                        for (auto & path : {"/etc/resolv.conf", "/etc/services", "/etc/hosts"})
                                            if (pathExists(path))
                                                ss.push_back(path);

                                        if (auto & caFile = fileTransferSettings.caFile.get()) {
                                            if (pathExists(*caFile))
                                                pathsInChroot.try_emplace(
                                                    "/etc/ssl/certs/ca-certificates.crt",
                                                    canonPath(caFile->native(), true),
                                                    true);
                                        }
                                    }

                                    for (auto & i : ss) {
                                        auto canonicalPath = canonPath(i, true);
                                        pathsInChroot.emplace(i, canonicalPath);
                                    }

                                    /* Bind-mount all the directories from the "host" filesystem. */
                                    for (auto & i : pathsInChroot) {
                                        if (i.second.source == "/proc")
                                            continue; // backwards compatibility

#if HAVE_EMBEDDED_SANDBOX_SHELL
                                        if (i.second.source == "__embedded_sandbox_shell__") {
                                            static unsigned char sh[] = {
#  include "embedded-sandbox-shell.gen.hh"
                                            };
                                            auto dst = chrootRootDir / i.first.relative_path();
                                            createDirs(dst.parent_path());
                                            writeFile(dst, std::string_view((const char *) sh, sizeof(sh)));
                                            chmod(dst, 0555);
                                        } else
#endif
                                        {
                                            doBind(
                                                i.second.source,
                                                chrootRootDir / i.first.relative_path(),
                                                i.second.optional);
                                        }
                                    }

                                    createDirs(chrootRootDir / "proc");
                                    if (mount("none", (chrootRootDir / "proc").c_str(), "proc", 0, 0) == -1)
                                        throw SysError("mounting /proc");

                                    if (buildUser && buildUser->getUIDCount() != 1) {
                                        createDirs(chrootRootDir / "sys");
                                        if (mount("none", (chrootRootDir / "sys").c_str(), "sysfs", 0, 0) == -1)
                                            throw SysError("mounting /sys");
                                    }

                                    if (pathExists("/dev/shm")
                                        && mount(
                                               "none",
                                               (chrootRootDir / "dev" / "shm").c_str(),
                                               "tmpfs",
                                               0,
                                               fmt("size=%s", store.config->getLocalSettings().sandboxShmSize).c_str())
                                               == -1)
                                        throw SysError("mounting /dev/shm");

                                    if (pathExists("/dev/pts/ptmx") && !pathExists(chrootRootDir / "dev" / "ptmx")
                                        && !pathsInChroot.count("/dev/pts")) {
                                        if (mount(
                                                "none",
                                                (chrootRootDir / "dev" / "pts").c_str(),
                                                "devpts",
                                                0,
                                                "newinstance,mode=0620")
                                            == 0) {
                                            createSymlink("/dev/pts/ptmx", chrootRootDir / "dev" / "ptmx");
                                            chmod(chrootRootDir / "dev" / "pts" / "ptmx", 0666);
                                        } else {
                                            if (errno != EINVAL)
                                                throw SysError("mounting /dev/pts");
                                            doBind("/dev/pts", chrootRootDir / "dev" / "pts");
                                            doBind("/dev/ptmx", chrootRootDir / "dev" / "ptmx");
                                        }
                                    }

                                    if (!drvOptions.useUidRange(drv))
                                        chmod(chrootRootDir / "etc", 0555);

                                    if (unshare(CLONE_NEWNS) == -1)
                                        throw SysError("unsharing mount namespace");

                                    if (cgroup && unshare(CLONE_NEWCGROUP) == -1)
                                        throw SysError("unsharing cgroup namespace");

                                    if (chdir(chrootRootDir.c_str()) == -1)
                                        throw SysError("cannot change directory to %1%", PathFmt(chrootRootDir));

                                    if (mkdir("real-root", 0500) == -1)
                                        throw SysError("cannot create real-root directory");

                                    if (pivot_root(".", "real-root") == -1)
                                        throw SysError(
                                            "cannot pivot old root directory onto %1%",
                                            PathFmt(chrootRootDir / "real-root"));

                                    if (chroot(".") == -1)
                                        throw SysError("cannot change root directory to %1%", PathFmt(chrootRootDir));

                                    if (umount2("real-root", MNT_DETACH) == -1)
                                        throw SysError("cannot unmount real root filesystem");

                                    if (rmdir("real-root") == -1)
                                        throw SysError("cannot remove real-root directory");

                                    setupSeccomp(localSettings);
                                    linux::setPersonality({
                                        .system = drv.platform,
                                        .impersonateLinux26 = localSettings.impersonateLinux26,
                                    });
                                }

                                if (chdir(tmpDirInSandbox().c_str()) == -1)
                                    throw SysError("changing into %1%", PathFmt(tmpDir));

                                unix::closeExtraFDs();

                                struct rlimit limit = {0, RLIM_INFINITY};
                                setrlimit(RLIMIT_CORE, &limit);

                                preserveDeathSignal([this]() {
                                    if (setgid(sandboxGid()) == -1)
                                        throw SysError("setgid failed");
                                    if (setuid(sandboxUid()) == -1)
                                        throw SysError("setuid failed");
                                });

                                writeFull(STDERR_FILENO, std::string("\2\n"));

                                sendException = false;

                                if (drv.isBuiltin())
                                    nix::runBuiltinBuilder(ctx, drv, scratchOutputs, store);

                                nix::execBuilder(drv, inputRewrites, env);

                            } catch (...) {
                                handleChildException(sendException);
                                _exit(1);
                            }
                        },
                        options);

                    writeFull(sendPid.writeSide.get(), fmt("%d\n", child));
                    _exit(0);
                } catch (...) {
                    handleChildException(true);
                    _exit(1);
                }
            });

            sendPid.writeSide.close();

            if (helper.wait() != 0) {
                nix::processSandboxSetupMessages(builderOut, pid, store, drvPath);
                // Only reached if the child process didn't send an exception.
                throw Error("unable to start build process");
            }

            userNamespaceSync.readSide = -1;

            bool userNamespaceSyncDone = false;
            Finally cleanup2([&]() {
                try {
                    if (!userNamespaceSyncDone)
                        writeFull(userNamespaceSync.writeSide.get(), "0\n");
                } catch (...) {
                }
                userNamespaceSync.writeSide = -1;
            });

            FdSource sendPidSource(sendPid.readSide.get());
            auto ss = tokenizeString<std::vector<std::string>>(sendPidSource.readLine());
            assert(ss.size() == 1);
            pid = string2Int<pid_t>(ss[0]).value();
            auto thisProcPath = procPath / std::to_string(static_cast<pid_t>(pid));

            if (usingUserNamespace) {
                uid_t hostUid = buildUser ? buildUser->getUID() : getuid();
                uid_t hostGid = buildUser ? buildUser->getGID() : getgid();
                uid_t nrIds = buildUser ? buildUser->getUIDCount() : 1;

                writeFile(thisProcPath / "uid_map", fmt("%d %d %d", sandboxUid(), hostUid, nrIds));

                if (!buildUser || buildUser->getUIDCount() == 1)
                    writeFile(thisProcPath / "setgroups", "deny");

                writeFile(thisProcPath / "gid_map", fmt("%d %d %d", sandboxGid(), hostGid, nrIds));
            } else {
                debug("note: not using a user namespace");
                if (!buildUser)
                    throw Error(
                        "cannot perform a sandboxed build because user namespaces are not enabled; check /proc/sys/user/max_user_namespaces");
            }

            writeFile(
                chrootRootDir / "etc" / "passwd",
                fmt("root:x:0:0:Nix build user:%3%:/noshell\n"
                    "nixbld:x:%1%:%2%:Nix build user:%3%:/noshell\n"
                    "nobody:x:65534:65534:Nobody:/:/noshell\n",
                    sandboxUid(),
                    sandboxGid(),
                    store.config->getLocalSettings().sandboxBuildDir));

            auto sandboxPath = thisProcPath / "ns";
            sandboxMountNamespace = open((sandboxPath / "mnt").c_str(), O_RDONLY);
            if (sandboxMountNamespace.get() == -1)
                throw SysError("getting sandbox mount namespace");

            if (usingUserNamespace) {
                sandboxUserNamespace = open((sandboxPath / "user").c_str(), O_RDONLY);
                if (sandboxUserNamespace.get() == -1)
                    throw SysError("getting sandbox user namespace");
            }

            if (cgroup)
                writeFile(*cgroup / "cgroup.procs", fmt("%d", (pid_t) pid));

            writeFull(userNamespaceSync.writeSide.get(), "1\n");
            userNamespaceSyncDone = true;
        }

        pid.setSeparatePG(true);

        nix::processSandboxSetupMessages(builderOut, pid, store, drvPath);

        return builderOut.get();
    }

    SingleDrvOutputs unprepareBuild() override
    {
        sandboxMountNamespace = -1;
        sandboxUserNamespace = -1;

        int status = nix::commonUnprepare(pid, store, drvPath, buildResult, *miscMethods, builderOut);

        killSandbox(true);

        nix::stopDaemon(daemonSocket, daemonThread, daemonWorkerThreads);

        nix::logCpuUsage(store, drvPath, buildResult, status);

        if (!statusOk(status)) {
            bool diskFull = nix::isDiskFull(store, tmpDir);

            cleanupBuild(false);

            throw BuilderFailureError{
                !derivationType.isSandboxed() || diskFull ? BuildResult::Failure::TransientFailure
                                                          : BuildResult::Failure::PermanentFailure,
                status,
                diskFull ? "\nnote: build failure may have been caused by lack of free disk space" : "",
            };
        }

        outputRewrites.clear();
        auto builtOutputs = nix::registerOutputs(
            store, localSettings, *this, addedPaths, scratchOutputs,
            outputRewrites, buildUser.get(), tmpDir,
            [this](const std::string & p) -> std::filesystem::path { return chrootRootDir / std::filesystem::path(p).relative_path(); });

        cleanupBuild(true);

        return builtOutputs;
    }
};

const std::filesystem::path LinuxChrootDerivationBuilder::homeDir = "/homeless-shelter";

DerivationBuilderUnique makeLinuxChrootDerivationBuilder(
    LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
{
    return DerivationBuilderUnique(new LinuxChrootDerivationBuilder(store, std::move(miscMethods), std::move(params)));
}

} // namespace nix
