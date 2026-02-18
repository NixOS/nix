#include "linux-chroot-derivation-builder.hh"
#include "linux-derivation-builder-common.hh"
#include "derivation-builder-common.hh"
#include "nix/store/build/derivation-builder.hh"
#include "nix/util/file-system.hh"
#include "nix/store/local-store.hh"
#include "nix/util/processes.hh"
#include "nix/store/builtins.hh"
#include "nix/store/path-references.hh"
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
#include "nix/store/filetransfer.hh"
#include "nix/store/personality.hh"
#include "nix/util/linux-namespaces.hh"
#include "build/derivation-check.hh"
#include "store-config-private.hh"
#include "chroot.hh"

#include <sys/un.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <sched.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/syscall.h>

#if HAVE_STATVFS
#  include <sys/statvfs.h>
#endif

#include <pwd.h>
#include <grp.h>

#include "nix/util/strings.hh"
#include "nix/util/signals.hh"
#include "nix/util/cgroup.hh"

#if NIX_WITH_AWS_AUTH
#  include "nix/store/aws-creds.hh"
#  include "nix/store/s3-url.hh"
#  include "nix/util/url.hh"
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
    /**
     * The process ID of the builder.
     */
    Pid pid;

    LocalStore & store;

    const LocalSettings & localSettings = store.config->getLocalSettings();

    std::unique_ptr<DerivationBuilderCallbacks> miscMethods;

    /**
     * User selected for running the builder.
     */
    std::unique_ptr<UserLock> buildUser;

    /**
     * The temporary directory used for the build.
     */
    std::filesystem::path tmpDir;

    /**
     * The top-level temporary directory.
     */
    std::filesystem::path topTmpDir;

    /**
     * The sort of derivation we are building.
     */
    const DerivationType derivationType;

    typedef StringMap Environment;
    Environment env;

    typedef std::map<StorePath, StorePath> RedirectedOutputs;
    RedirectedOutputs redirectedOutputs;

    /**
     * The output paths used during the build.
     */
    OutputPathMap scratchOutputs;

    static const std::filesystem::path homeDir;

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

    /**
     * The chroot root directory.
     */
    std::filesystem::path chrootRootDir;

    /**
     * RAII cleanup for the chroot directory.
     */
    std::optional<AutoDelete> autoDelChroot;

    PathsInChroot pathsInChroot;

    /**
     * Pipe for synchronising updates to the builder namespaces.
     */
    Pipe userNamespaceSync;

    /**
     * The mount namespace and user namespace of the builder, used to add additional
     * paths to the sandbox as a result of recursive Nix calls.
     */
    AutoCloseFD sandboxMountNamespace;
    AutoCloseFD sandboxUserNamespace;

    /**
     * On Linux, whether we're doing the build in its own user namespace.
     */
    bool usingUserNamespace = true;

    /**
     * The cgroup of the builder, if any.
     */
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

    /**
     * Arguments passed to child process lambda.
     */
    struct RunChildArgs
    {
#if NIX_WITH_AWS_AUTH
        std::optional<AwsCredentials> awsCredentials;
#endif
    };


    void processSandboxSetupMessages()
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

    void stopDaemon()
    {
        if (daemonSocket && shutdown(daemonSocket.get(), SHUT_RDWR) == -1) {
            if (errno == ENOTCONN) {
                daemonSocket.close();
            } else {
                throw SysError("shutting down daemon socket");
            }
        }

        if (daemonThread.joinable())
            daemonThread.join();

        for (auto & thread : daemonWorkerThreads)
            thread.join();
        daemonWorkerThreads.clear();

        daemonSocket.close();
    }

    void addDependencyImpl(const StorePath & path) override
    {
        addedPaths.insert(path);

        debug("materialising '%s' in the sandbox", store.printStorePath(path));

        std::filesystem::path source = store.toRealPath(path);
        std::filesystem::path target =
            chrootRootDir / std::filesystem::path(store.printStorePath(path)).relative_path();

        if (pathExists(target)) {
            // There is a similar debug message in doBind, so only run it in this block to not have double messages.
            debug("bind-mounting %s -> %s", PathFmt(target), PathFmt(source));
            throw Error("store path '%s' already exists in the sandbox", store.printStorePath(path));
        }

        /* Bind-mount the path into the sandbox. This requires
           entering its mount namespace, which is not possible
           in multithreaded programs. So we do this in a
           child process.*/
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

    void chownToBuilder(const std::filesystem::path & path)
    {
        nix::chownToBuilder(buildUser.get(), path);
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


    StorePath makeFallbackPath(OutputNameView outputName)
    {
        auto pathType = "rewrite:" + std::string(drvPath.to_string()) + ":name:" + std::string(outputName);
        return store.makeStorePath(pathType, Hash(HashAlgorithm::SHA256), outputPathName(drv.name, outputName));
    }

    StorePath makeFallbackPath(const StorePath & path)
    {
        auto pathType =
            "rewrite:" + std::string(drvPath.to_string()) + ":" + std::string(path.to_string());
        return store.makeStorePath(pathType, Hash(HashAlgorithm::SHA256), path.name());
    }
    void cleanupBuild(bool force)
    {
        if (force) {
            for (auto & i : redirectedOutputs)
                deletePath(store.toRealPath(i.second));
        }

        if (topTmpDir != "") {
            chmod(topTmpDir, 0000);

            if (settings.keepFailed && !force && !drv.isBuiltin()) {
                printError("note: keeping build directory %s", PathFmt(tmpDir));
                chmod(topTmpDir, 0755);
                chmod(tmpDir, 0755);
            } else
                deletePath(topTmpDir);
            topTmpDir = "";
            tmpDir = "";
        }

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

            /* If we're running from the daemon, then this will return the
               root cgroup of the service. Otherwise, it will return the
               current cgroup. */
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

            /* When using a build user, record the cgroup we used for that
               user so that if we got interrupted previously, we can kill
               any left-over cgroup first. */
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

        AutoCloseFD tmpDirFd{open(tmpDir.c_str(), O_RDONLY | O_NOFOLLOW | O_DIRECTORY)};
        if (!tmpDirFd)
            throw SysError("failed to open the build temporary directory descriptor %1%", PathFmt(tmpDir));

        nix::chownToBuilder(buildUser.get(), tmpDirFd.get(), tmpDir);

        StringMap inputRewrites;
        for (auto & [outputName, status] : initialOutputs) {
            auto scratchPath = !status.known ? makeFallbackPath(outputName)
                               : !needsHashRewrite()
                                   ? status.known->path
                                   : !status.known->isPresent()
                                         ? status.known->path
                                         : buildMode != bmRepair && !status.known->isValid()
                                               ? status.known->path
                                               : makeFallbackPath(status.known->path);
            scratchOutputs.insert_or_assign(outputName, scratchPath);

            inputRewrites[hashPlaceholder(outputName)] = store.printStorePath(scratchPath);

            if (!status.known)
                continue;
            auto fixedFinalPath = status.known->path;

            if (fixedFinalPath == scratchPath)
                continue;

            deletePath(store.printStorePath(scratchPath));

            {
                std::string h1{fixedFinalPath.hashPart()};
                std::string h2{scratchPath.hashPart()};
                inputRewrites[h1] = h2;
            }

            redirectedOutputs.insert_or_assign(std::move(fixedFinalPath), std::move(scratchPath));
        }

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

        {
            PathSet allowedPaths = localSettings.allowedImpureHostPrefixes;

            auto impurePaths = drvOptions.impureHostDeps;

            for (auto & i : impurePaths) {
                bool found = false;
                std::filesystem::path canonI = canonPath(i);
                for (auto & a : allowedPaths) {
                    std::filesystem::path canonA = canonPath(a);
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

                pathsInChroot[i] = {i, true};
            }
        }

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
            /* If the name isn't known a priori (i.e. floating
               content-addressing derivation), the temporary location we use
               should be fresh.  Freshness means it is impossible that the path
               is already in the sandbox, so we don't need to worry about
               removing it.  */
            if (i.second.second)
                pathsInChroot.erase(store.printStorePath(*i.second.second));
        }

        // Set up chroot parameters
        BuildChrootParams params{
            .chrootParentDir = store.toRealPath(drvPath) + ".chroot",
            .useUidRange = drvOptions.useUidRange(drv),
            .isSandboxed = derivationType.isSandboxed(),
            .buildUser = buildUser.get(),
            .storeDir = store.storeDir,
            .chownToBuilder = [this](const std::filesystem::path & path) { this->chownToBuilder(path); },
            .getSandboxGid = [this]() { return this->sandboxGid(); },
        };

        // Create the chroot
        auto [rootDir, cleanup] = setupBuildChroot(params);
        chrootRootDir = std::move(rootDir);
        autoDelChroot.emplace(std::move(cleanup));

        if (localSettings.preBuildHook != "") {
            printMsg(lvlChatty, "executing pre-build hook '%1%'", localSettings.preBuildHook);

            enum BuildHookState { stBegin, stExtraChrootDirs };

            auto state = stBegin;
            assert(!chrootRootDir.empty());
            auto lines = runProgram(
                localSettings.preBuildHook, false, Strings({store.printStorePath(drvPath), chrootRootDir.native()}));
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

        if (cgroup) {
            if (mkdir(cgroup->c_str(), 0755) != 0)
                throw SysError("creating cgroup %s", PathFmt(*cgroup));
            chownToBuilder(*cgroup);
            chownToBuilder(*cgroup / "cgroup.procs");
            chownToBuilder(*cgroup / "cgroup.threads");
            // chownToBuilder(*cgroup / "cgroup.subtree_control");
        }

        if (needsHashRewrite() && pathExists(homeDir))
            throw Error(
                "home directory %1% exists; please remove it to assure purity of builds without sandboxing",
                PathFmt(homeDir));

        if (drvOptions.getRequiredSystemFeatures(drv).count("recursive-nix")) {
            experimentalFeatureSettings.require(Xp::RecursiveNix);

            auto restrictedStore = makeRestrictedStore(
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
            std::filesystem::path socketPath = tmpDir / socketName;
            env["NIX_REMOTE"] = "unix://" + (tmpDirInSandbox() / socketName).native();

            daemonSocket = createUnixDomainSocket(socketPath, 0600);

            chownToBuilder(socketPath);

            daemonThread = std::thread([this, restrictedStore]() {
                while (true) {
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

                    auto workerThread = std::thread([restrictedStore, remote{std::move(remote)}]() {
                        try {
                            daemon::processConnection(
                                restrictedStore,
                                FdSource(remote.get()),
                                FdSink(remote.get()),
                                NotTrusted,
                                daemon::Recursive);
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

        printMsg(lvlChatty, "executing builder '%1%'", drv.builder);
        printMsg(lvlChatty, "using builder args '%1%'", concatStringsSep(" ", drv.args));
        for (auto & i : drv.env)
            printMsg(lvlVomit, "setting builder env variable '%1%'='%2%'", i.first, i.second);

        miscMethods->openLogFile();

        builderOut = posix_openpt(O_RDWR | O_NOCTTY);
        if (!builderOut)
            throw SysError("opening pseudoterminal master");

        std::string slaveName = getPtsName(builderOut.get());

        if (buildUser) {
            chmod(slaveName, 0600);

            if (chown(slaveName.c_str(), buildUser->getUID(), 0))
                throw SysError("changing owner of pseudoterminal slave");
        }

        if (unlockpt(builderOut.get()))
            throw SysError("unlocking pseudoterminal");

        buildResult.startTime = time(0);

        // startChild() inlined
        {
            RunChildArgs args{
#if NIX_WITH_AWS_AUTH
                .awsCredentials = [&]() -> std::optional<AwsCredentials> {
                    if (drv.isBuiltin() && drv.builder == "builtin:fetchurl") {
                        auto url = drv.env.find("url");
                        if (url != drv.env.end()) {
                            try {
                                auto parsedUrl = parseURL(url->second);
                                if (parsedUrl.scheme == "s3") {
                                    debug("Pre-resolving AWS credentials for S3 URL in builtin:fetchurl");
                                    auto s3Url = ParsedS3URL::parse(parsedUrl);
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
                }(),
#endif
            };

            userNamespaceSync.create();

            usingUserNamespace = userNamespacesSupported();

            Pipe sendPid;
            sendPid.create();

            Pid helper = startProcess([&]() {
                sendPid.readSide.close();

                /* We need to open the slave early, before
                   CLONE_NEWUSER. Otherwise we get EPERM when running as
                   root. */
                {
                    std::string slaveName = getPtsName(builderOut.get());

                    AutoCloseFD slaveOut = open(slaveName.c_str(), O_RDWR | O_NOCTTY);
                    if (!slaveOut)
                        throw SysError("opening pseudoterminal slave");

                    struct termios term;
                    if (tcgetattr(slaveOut.get(), &term))
                        throw SysError("getting pseudoterminal attributes");

                    cfmakeraw(&term);

                    if (tcsetattr(slaveOut.get(), TCSANOW, &term))
                        throw SysError("putting pseudoterminal into raw mode");

                    if (dup2(slaveOut.get(), STDERR_FILENO) == -1)
                        throw SysError("cannot pipe standard error into log file");
                }

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
                        [this, &inputRewrites, args = std::move(args)]() {
                            // runChild inlined
                            bool sendException = true;

                            try {
                                commonChildInit();

                                BuiltinBuilderContext ctx{
                                    .drv = drv,
                                    .hashedMirrors = settings.getLocalSettings().hashedMirrors,
                                    .tmpDirInSandbox = tmpDirInSandbox(),
#if NIX_WITH_AWS_AUTH
                                    .awsCredentials = args.awsCredentials,
#endif
                                };

                                if (drv.isBuiltin() && drv.builder == "builtin:fetchurl") {
                                    try {
                                        ctx.netrcData = readFile(fileTransferSettings.netrcFile);
                                    } catch (SystemError &) {
                                    }

                                    if (auto & caFile = fileTransferSettings.caFile.get())
                                        try {
                                            ctx.caFileData = readFile(*caFile);
                                        } catch (SystemError &) {
                                        }
                                }

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

                                    /* Bind-mount the sandbox's Nix store onto itself so that
                                       we can mark it as a "shared" subtree. */
                                    std::filesystem::path chrootStoreDir =
                                        chrootRootDir / std::filesystem::path(store.storeDir).relative_path();

                                    if (mount(chrootStoreDir.c_str(), chrootStoreDir.c_str(), 0, MS_BIND, 0) == -1)
                                        throw SysError(
                                            "unable to bind mount the Nix store at %1%", PathFmt(chrootStoreDir));

                                    if (mount(0, chrootStoreDir.c_str(), 0, MS_SHARED, 0) == -1)
                                        throw SysError("unable to make %s shared", PathFmt(chrootStoreDir));

                                    /* Set up a nearly empty /dev, unless the user asked to
                                       bind-mount the host /dev. */
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

                                    /* Fixed-output derivations typically need to access the network. */
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

                                    /* Bind a new instance of procfs on /proc. */
                                    createDirs(chrootRootDir / "proc");
                                    if (mount("none", (chrootRootDir / "proc").c_str(), "proc", 0, 0) == -1)
                                        throw SysError("mounting /proc");

                                    /* Mount sysfs on /sys. */
                                    if (buildUser && buildUser->getUIDCount() != 1) {
                                        createDirs(chrootRootDir / "sys");
                                        if (mount("none", (chrootRootDir / "sys").c_str(), "sysfs", 0, 0) == -1)
                                            throw SysError("mounting /sys");
                                    }

                                    /* Mount a new tmpfs on /dev/shm. */
                                    if (pathExists("/dev/shm")
                                        && mount(
                                               "none",
                                               (chrootRootDir / "dev" / "shm").c_str(),
                                               "tmpfs",
                                               0,
                                               fmt("size=%s", store.config->getLocalSettings().sandboxShmSize).c_str())
                                               == -1)
                                        throw SysError("mounting /dev/shm");

                                    /* Mount a new devpts on /dev/pts. */
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

                                    /* Make /etc unwritable. */
                                    if (!drvOptions.useUidRange(drv))
                                        chmod(chrootRootDir / "etc", 0555);

                                    /* Unshare this mount namespace. */
                                    if (unshare(CLONE_NEWNS) == -1)
                                        throw SysError("unsharing mount namespace");

                                    /* Unshare the cgroup namespace. */
                                    if (cgroup && unshare(CLONE_NEWCGROUP) == -1)
                                        throw SysError("unsharing cgroup namespace");

                                    /* Do the chroot(). */
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

                                    /* Apply seccomp and personality. */
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
                                    /* Switch to the sandbox uid/gid in the user namespace. */
                                    if (setgid(sandboxGid()) == -1)
                                        throw SysError("setgid failed");
                                    if (setuid(sandboxUid()) == -1)
                                        throw SysError("setuid failed");
                                });

                                writeFull(STDERR_FILENO, std::string("\2\n"));

                                sendException = false;

                                if (drv.isBuiltin()) {
                                    try {
                                        logger = makeJSONLogger(getStandardError());

                                        for (auto & e : drv.outputs)
                                            ctx.outputs.insert_or_assign(
                                                e.first, store.printStorePath(scratchOutputs.at(e.first)));

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

                                Strings buildArgs;
                                buildArgs.push_back(std::string(baseNameOf(drv.builder)));

                                for (auto & i : drv.args)
                                    buildArgs.push_back(rewriteStrings(i, inputRewrites));

                                Strings envStrs;
                                for (auto & i : env)
                                    envStrs.push_back(rewriteStrings(i.first + "=" + i.second, inputRewrites));

                                execve(
                                    drv.builder.c_str(),
                                    stringsToCharPtrs(buildArgs).data(),
                                    stringsToCharPtrs(envStrs).data());

                                throw SysError("executing '%1%'", drv.builder);

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
                processSandboxSetupMessages();
                // Only reached if the child process didn't send an exception.
                throw Error("unable to start build process");
            }

            userNamespaceSync.readSide = -1;

            /* Make sure that we write *something* to the child in case of
               an exception. */
            bool userNamespaceSyncDone = false;
            Finally cleanup([&]() {
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

            /* Now that we know the sandbox uid, we can write /etc/passwd. */
            writeFile(
                chrootRootDir / "etc" / "passwd",
                fmt("root:x:0:0:Nix build user:%3%:/noshell\n"
                    "nixbld:x:%1%:%2%:Nix build user:%3%:/noshell\n"
                    "nobody:x:65534:65534:Nobody:/:/noshell\n",
                    sandboxUid(),
                    sandboxGid(),
                    store.config->getLocalSettings().sandboxBuildDir));

            /* Save the mount- and user namespace of the child. */
            auto sandboxPath = thisProcPath / "ns";
            sandboxMountNamespace = open((sandboxPath / "mnt").c_str(), O_RDONLY);
            if (sandboxMountNamespace.get() == -1)
                throw SysError("getting sandbox mount namespace");

            if (usingUserNamespace) {
                sandboxUserNamespace = open((sandboxPath / "user").c_str(), O_RDONLY);
                if (sandboxUserNamespace.get() == -1)
                    throw SysError("getting sandbox user namespace");
            }

            /* Move the child into its own cgroup. */
            if (cgroup)
                writeFile(*cgroup / "cgroup.procs", fmt("%d", (pid_t) pid));

            /* Signal the builder that we've updated its user namespace. */
            writeFull(userNamespaceSync.writeSide.get(), "1\n");
            userNamespaceSyncDone = true;
        }

        pid.setSeparatePG(true);

        processSandboxSetupMessages();

        return builderOut.get();
    }


    SingleDrvOutputs unprepareBuild() override
    {
        sandboxMountNamespace = -1;
        sandboxUserNamespace = -1;

        int status = pid.kill();

        debug("builder process for '%s' finished", store.printStorePath(drvPath));

        buildResult.timesBuilt++;
        buildResult.stopTime = time(0);

        miscMethods->childTerminated();

        builderOut.close();

        miscMethods->closeLogFile();

        killSandbox(true);

        stopDaemon();

        if (buildResult.cpuUser && buildResult.cpuSystem) {
            debug(
                "builder for '%s' terminated with status %d, user CPU %.3fs, system CPU %.3fs",
                store.printStorePath(drvPath),
                status,
                ((double) buildResult.cpuUser->count()) / 1000000,
                ((double) buildResult.cpuSystem->count()) / 1000000);
        }

        if (!statusOk(status)) {
            bool diskFull = false;
#if HAVE_STATVFS
            {
                uint64_t required = 8ULL * 1024 * 1024;
                struct statvfs st;
                if (statvfs(store.config->realStoreDir.get().c_str(), &st) == 0
                    && (uint64_t) st.f_bavail * st.f_bsize < required)
                    diskFull = true;
                if (statvfs(tmpDir.c_str(), &st) == 0 && (uint64_t) st.f_bavail * st.f_bsize < required)
                    diskFull = true;
            }
#endif

            cleanupBuild(false);

            throw BuilderFailureError{
                !derivationType.isSandboxed() || diskFull ? BuildResult::Failure::TransientFailure
                                                          : BuildResult::Failure::PermanentFailure,
                status,
                diskFull ? "\nnote: build failure may have been caused by lack of free disk space" : "",
            };
        }

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
