#include "linux-derivation-builder.hh"
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

#include <sys/un.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/prctl.h>

#if HAVE_SECCOMP
#  include <seccomp.h>
#endif
#include "linux/fchmodat2-compat.hh"

#if HAVE_STATVFS
#  include <sys/statvfs.h>
#endif

#include <pwd.h>
#include <grp.h>

#include "nix/util/strings.hh"
#include "nix/util/signals.hh"

#if NIX_WITH_AWS_AUTH
#  include "nix/store/aws-creds.hh"
#  include "nix/store/s3-url.hh"
#  include "nix/util/url.hh"
#endif

namespace nix {

using namespace nix::linux;

struct NotDeterministicLinux : BuildError
{
    NotDeterministicLinux(auto &&... args)
        : BuildError(BuildResult::Failure::NotDeterministic, args...)
    {
        isNonDeterministic = true;
    }
};

static void handleDiffHookLinux(
    const Path & diffHook,
    uid_t uid,
    uid_t gid,
    const std::filesystem::path & tryA,
    const std::filesystem::path & tryB,
    const std::filesystem::path & drvPath,
    const std::filesystem::path & tmpDir)
{
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
            throw ExecError(
                diffRes.first, "diff-hook program %s %2%", PathFmt(diffHook), statusToString(diffRes.first));

        if (diffRes.second != "")
            printError(chomp(diffRes.second));
    } catch (Error & error) {
        ErrorInfo ei = error.info();
        ei.msg = HintFmt("diff hook execution failed: %s", ei.msg.str());
        logError(ei);
    }
}

static void rethrowExceptionAsErrorLinux()
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

static void handleChildExceptionLinux(bool sendException)
{
    try {
        rethrowExceptionAsErrorLinux();
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

static void checkNotWorldWritableLinux(std::filesystem::path path)
{
    while (true) {
        auto st = lstat(path);
        if (st.st_mode & S_IWOTH)
            throw Error("Path %s is world-writable or a symlink. That's not allowed for security.", PathFmt(path));
        if (path == path.parent_path())
            break;
        path = path.parent_path();
    }
    return;
}

static void movePath(const std::filesystem::path & src, const std::filesystem::path & dst)
{
    auto st = lstat(src);

    bool changePerm = (geteuid() && S_ISDIR(st.st_mode) && !(st.st_mode & S_IWUSR));

    if (changePerm)
        chmod(src, st.st_mode | S_IWUSR);

    std::filesystem::rename(src, dst);

    if (changePerm)
        chmod(dst, st.st_mode);
}

static void replaceValidPath(const std::filesystem::path & storePath, const std::filesystem::path & tmpPath)
{
    std::filesystem::path oldPath;

    if (pathExists(storePath)) {
        do {
            oldPath = makeTempPath(storePath, ".old");
        } while (pathExists(oldPath));
        movePath(storePath, oldPath);
    }
    try {
        movePath(tmpPath, storePath);
    } catch (...) {
        try {
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

static void setupSeccomp(const LocalSettings & localSettings)
{
    if (!localSettings.filterSyscalls)
        return;

#if HAVE_SECCOMP
    scmp_filter_ctx ctx;

    if (!(ctx = seccomp_init(SCMP_ACT_ALLOW)))
        throw SysError("unable to initialize seccomp mode 2");

    Finally cleanup([&]() { seccomp_release(ctx); });

    constexpr std::string_view nativeSystem = NIX_LOCAL_SYSTEM;

    if (nativeSystem == "x86_64-linux" && seccomp_arch_add(ctx, SCMP_ARCH_X86) != 0)
        throw SysError("unable to add 32-bit seccomp architecture");

    if (nativeSystem == "x86_64-linux" && seccomp_arch_add(ctx, SCMP_ARCH_X32) != 0)
        throw SysError("unable to add X32 seccomp architecture");

    if (nativeSystem == "aarch64-linux" && seccomp_arch_add(ctx, SCMP_ARCH_ARM) != 0)
        printError(
            "unable to add ARM seccomp architecture; this may result in spurious build failures if running 32-bit ARM processes");

    if (nativeSystem == "mips64-linux" && seccomp_arch_add(ctx, SCMP_ARCH_MIPS) != 0)
        printError("unable to add mips seccomp architecture");

    if (nativeSystem == "mips64-linux" && seccomp_arch_add(ctx, SCMP_ARCH_MIPS64N32) != 0)
        printError("unable to add mips64-*abin32 seccomp architecture");

    if (nativeSystem == "mips64el-linux" && seccomp_arch_add(ctx, SCMP_ARCH_MIPSEL) != 0)
        printError("unable to add mipsel seccomp architecture");

    if (nativeSystem == "mips64el-linux" && seccomp_arch_add(ctx, SCMP_ARCH_MIPSEL64N32) != 0)
        printError("unable to add mips64el-*abin32 seccomp architecture");

    /* Prevent builders from creating setuid/setgid binaries. */
    for (int perm : {S_ISUID, S_ISGID}) {
        if (seccomp_rule_add(
                ctx,
                SCMP_ACT_ERRNO(EPERM),
                SCMP_SYS(chmod),
                1,
                SCMP_A1(SCMP_CMP_MASKED_EQ, (scmp_datum_t) perm, (scmp_datum_t) perm))
            != 0)
            throw SysError("unable to add seccomp rule");

        if (seccomp_rule_add(
                ctx,
                SCMP_ACT_ERRNO(EPERM),
                SCMP_SYS(fchmod),
                1,
                SCMP_A1(SCMP_CMP_MASKED_EQ, (scmp_datum_t) perm, (scmp_datum_t) perm))
            != 0)
            throw SysError("unable to add seccomp rule");

        if (seccomp_rule_add(
                ctx,
                SCMP_ACT_ERRNO(EPERM),
                SCMP_SYS(fchmodat),
                1,
                SCMP_A2(SCMP_CMP_MASKED_EQ, (scmp_datum_t) perm, (scmp_datum_t) perm))
            != 0)
            throw SysError("unable to add seccomp rule");

        if (seccomp_rule_add(
                ctx,
                SCMP_ACT_ERRNO(EPERM),
                NIX_SYSCALL_FCHMODAT2,
                1,
                SCMP_A2(SCMP_CMP_MASKED_EQ, (scmp_datum_t) perm, (scmp_datum_t) perm))
            != 0)
            throw SysError("unable to add seccomp rule");
    }

    /* Prevent builders from using EAs or ACLs. */
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(getxattr), 0) != 0
        || seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(lgetxattr), 0) != 0
        || seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(fgetxattr), 0) != 0
        || seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(setxattr), 0) != 0
        || seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(lsetxattr), 0) != 0
        || seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(fsetxattr), 0) != 0)
        throw SysError("unable to add seccomp rule");

    if (seccomp_attr_set(ctx, SCMP_FLTATR_CTL_NNP, localSettings.allowNewPrivileges ? 0 : 1) != 0)
        throw SysError("unable to set 'no new privileges' seccomp attribute");

    if (seccomp_load(ctx) != 0)
        throw SysError("unable to load seccomp BPF program");
#else
    throw Error(
        "seccomp is not supported on this platform; "
        "you can bypass this error by setting the option 'filter-syscalls' to false, but note that untrusted builds can then create setuid binaries!");
#endif
}

struct LinuxDerivationBuilder : DerivationBuilder, DerivationBuilderParams
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

    LinuxDerivationBuilder(
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
        return true;
    }

    std::unique_ptr<UserLock> getBuildUser()
    {
        return acquireUserLock(settings.nixStateDir, localSettings, 1, false);
    }

    PathsInChroot getPathsInSandbox()
    {
        PathsInChroot pathsInChroot = defaultPathsInChroot;

        if (hasPrefix(store.storeDir, tmpDirInSandbox().native())) {
            throw Error("`sandbox-build-dir` must not contain the storeDir");
        }
        pathsInChroot[tmpDirInSandbox()] = {.source = tmpDir};

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

        if (localSettings.preBuildHook != "") {
            printMsg(lvlChatty, "executing pre-build hook '%1%'", localSettings.preBuildHook);

            enum BuildHookState { stBegin, stExtraChrootDirs };

            auto state = stBegin;
            auto lines = runProgram(localSettings.preBuildHook, false, getPreBuildHookArgs());
            auto lastPos = std::string::size_type{0};
            for (auto nlPos = lines.find('\n'); nlPos != std::string::npos;
                 nlPos = lines.find('\n', lastPos)) {
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

    void setBuildTmpDir()
    {
        tmpDir = topTmpDir;
    }

    std::filesystem::path tmpDirInSandbox()
    {
        assert(!topTmpDir.empty());
        return topTmpDir;
    }

    void prepareUser()
    {
        killSandbox(false);
    }

    void prepareSandbox()
    {
        if (drvOptions.useUidRange(drv))
            throw Error("feature 'uid-range' is not supported on this platform");
    }

    Strings getPreBuildHookArgs()
    {
        return Strings({store.printStorePath(drvPath)});
    }

    std::filesystem::path realPathInHost(const std::filesystem::path & p)
    {
        return store.toRealPath(p.native());
    }

    void openSlave()
    {
        std::string slaveName = getPtsName(builderOut.get());

        AutoCloseFD builderOut = open(slaveName.c_str(), O_RDWR | O_NOCTTY);
        if (!builderOut)
            throw SysError("opening pseudoterminal slave");

        struct termios term;
        if (tcgetattr(builderOut.get(), &term))
            throw SysError("getting pseudoterminal attributes");

        cfmakeraw(&term);

        if (tcsetattr(builderOut.get(), TCSANOW, &term))
            throw SysError("putting pseudoterminal into raw mode");

        if (dup2(builderOut.get(), STDERR_FILENO) == -1)
            throw SysError("cannot pipe standard error into log file");
    }

    void enterChroot()
    {
        setupSeccomp(localSettings);

        linux::setPersonality({
            .system = drv.platform,
            .impersonateLinux26 = localSettings.impersonateLinux26,
        });
    }

    void setUser()
    {
        if (buildUser) {
            preserveDeathSignal([this]() {
                auto gids = buildUser->getSupplementaryGIDs();
                if (setgroups(gids.size(), gids.data()) == -1)
                    throw SysError("cannot set supplementary groups of build user");

                if (setgid(buildUser->getGID()) == -1 || getgid() != buildUser->getGID()
                    || getegid() != buildUser->getGID())
                    throw SysError("setgid failed");

                if (setuid(buildUser->getUID()) == -1 || getuid() != buildUser->getUID()
                    || geteuid() != buildUser->getUID())
                    throw SysError("setuid failed");
            });
        }
    }

    void execBuilder(const Strings & args, const Strings & envStrs)
    {
        execve(drv.builder.c_str(), stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());
    }

    /**
     * Arguments passed to runChild().
     */
    struct RunChildArgs
    {
#if NIX_WITH_AWS_AUTH
        std::optional<AwsCredentials> awsCredentials;
#endif
    };

#if NIX_WITH_AWS_AUTH
    std::optional<AwsCredentials> preResolveAwsCredentials()
    {
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
    }
#endif

    void runChild(RunChildArgs args)
    {
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

            enterChroot();

            if (chdir(tmpDirInSandbox().c_str()) == -1)
                throw SysError("changing into %1%", PathFmt(tmpDir));

            unix::closeExtraFDs();

            struct rlimit limit = {0, RLIM_INFINITY};
            setrlimit(RLIMIT_CORE, &limit);

            setUser();

            writeFull(STDERR_FILENO, std::string("\2\n"));

            sendException = false;

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

            Strings buildArgs;
            buildArgs.push_back(std::string(baseNameOf(drv.builder)));

            for (auto & i : drv.args)
                buildArgs.push_back(rewriteStrings(i, inputRewrites));

            Strings envStrs;
            for (auto & i : env)
                envStrs.push_back(rewriteStrings(i.first + "=" + i.second, inputRewrites));

            execBuilder(buildArgs, envStrs);

            throw SysError("executing '%1%'", drv.builder);

        } catch (...) {
            handleChildExceptionLinux(sendException);
            _exit(1);
        }
    }

    void startChild()
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

    void initEnv()
    {
        nix::initEnv(
            env, homeDir, store.storeDir, *this, inputRewrites,
            derivationType, localSettings, tmpDirInSandbox(),
            buildUser.get(), tmpDir, tmpDirFd.get());
    }

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

    void startDaemon()
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
        std::filesystem::path socketPath = tmpDir / socketName;
        env["NIX_REMOTE"] = "unix://" + (tmpDirInSandbox() / socketName).native();

        daemonSocket = createUnixDomainSocket(socketPath, 0600);

        chownToBuilder(socketPath);

        daemonThread = std::thread([this, store]() {
            while (true) {
                struct sockaddr_un remoteAddr;
                socklen_t remoteAddrLen = sizeof(remoteAddr);

                AutoCloseFD remote =
                    accept(daemonSocket.get(), (struct sockaddr *) &remoteAddr, &remoteAddrLen);
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
                            store,
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
    }

    void chownToBuilder(const std::filesystem::path & path)
    {
        nix::chownToBuilder(buildUser.get(), path);
    }

    void chownToBuilder(int fd, const std::filesystem::path & path)
    {
        nix::chownToBuilder(buildUser.get(), fd, path);
    }

    void writeBuilderFile(const std::string & name, std::string_view contents)
    {
        nix::writeBuilderFile(buildUser.get(), tmpDir, tmpDirFd.get(), name, contents);
    }

    void killSandbox(bool getStats)
    {
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

    bool decideWhetherDiskFull()
    {
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
        return diskFull;
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
    }

    std::optional<Descriptor> startBuild() override
    {
        if (useBuildUsers(localSettings)) {
            if (!buildUser)
                buildUser = getBuildUser();

            if (!buildUser)
                return std::nullopt;
        }

        prepareUser();

        auto buildDir = store.config->getBuildDir();

        createDirs(buildDir);

        if (buildUser)
            checkNotWorldWritable(buildDir);

        topTmpDir = createTempDir(buildDir, "nix", 0700);
        setBuildTmpDir();
        assert(!tmpDir.empty());

        AutoCloseFD tmpDirFd{open(tmpDir.c_str(), O_RDONLY | O_NOFOLLOW | O_DIRECTORY)};
        if (!tmpDirFd)
            throw SysError("failed to open the build temporary directory descriptor %1%", PathFmt(tmpDir));

        chownToBuilder(tmpDirFd.get(), tmpDir);

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

        initEnv();

        prepareSandbox();

        if (needsHashRewrite() && pathExists(homeDir))
            throw Error(
                "home directory %1% exists; please remove it to assure purity of builds without sandboxing",
                PathFmt(homeDir));

        if (drvOptions.getRequiredSystemFeatures(drv).count("recursive-nix"))
            startDaemon();

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

        startChild();

        pid.setSeparatePG(true);

        processSandboxSetupMessages();

        return builderOut.get();
    }

    SingleDrvOutputs registerOutputs()
    {
        return nix::registerOutputs(
            store, localSettings, *this, addedPaths, scratchOutputs,
            outputRewrites, buildUser.get(), tmpDir,
            [this](const std::string & p) { return realPathInHost(p); });
    }

    SingleDrvOutputs unprepareBuild() override
    {
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
            bool diskFull = decideWhetherDiskFull();

            cleanupBuild(false);

            throw BuilderFailureError{
                !derivationType.isSandboxed() || diskFull ? BuildResult::Failure::TransientFailure
                                                          : BuildResult::Failure::PermanentFailure,
                status,
                diskFull ? "\nnote: build failure may have been caused by lack of free disk space" : "",
            };
        }

        auto builtOutputs = registerOutputs();

        cleanupBuild(true);

        return builtOutputs;
    }
};

const std::filesystem::path LinuxDerivationBuilder::homeDir = "/homeless-shelter";

DerivationBuilderUnique makeLinuxDerivationBuilder(
    LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
{
    return DerivationBuilderUnique(new LinuxDerivationBuilder(store, std::move(miscMethods), std::move(params)));
}

} // namespace nix
