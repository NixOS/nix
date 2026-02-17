#ifdef __APPLE__

#  include <spawn.h>
#  include <sys/sysctl.h>
#  include <sandbox.h>
#  include <sys/ipc.h>
#  include <sys/shm.h>
#  include <sys/msg.h>
#  include <sys/sem.h>

#  include "darwin-derivation-builder.hh"
#  include "derivation-builder-common.hh"
#  include "nix/store/build/derivation-builder.hh"
#  include "nix/util/file-system.hh"
#  include "nix/store/local-store.hh"
#  include "nix/util/processes.hh"
#  include "nix/store/builtins.hh"
#  include "nix/store/path-references.hh"
#  include "nix/util/util.hh"
#  include "nix/util/archive.hh"
#  include "nix/util/git.hh"
#  include "nix/store/daemon.hh"
#  include "nix/util/topo-sort.hh"
#  include "nix/store/build/child.hh"
#  include "nix/util/unix-domain-socket.hh"
#  include "nix/store/posix-fs-canonicalise.hh"
#  include "nix/util/posix-source-accessor.hh"
#  include "nix/store/restricted-store.hh"
#  include "nix/store/user-lock.hh"
#  include "nix/store/globals.hh"
#  include "nix/store/build/derivation-env-desugar.hh"
#  include "nix/util/terminal.hh"
#  include "nix/store/filetransfer.hh"
#  include "build/derivation-check.hh"
#  include "store-config-private.hh"

#  include <sys/un.h>
#  include <fcntl.h>
#  include <termios.h>
#  include <unistd.h>
#  include <sys/mman.h>
#  include <sys/resource.h>
#  include <sys/socket.h>

#  if HAVE_STATVFS
#    include <sys/statvfs.h>
#  endif

#  include <pwd.h>
#  include <grp.h>

#  include "nix/util/strings.hh"
#  include "nix/util/signals.hh"

#  if NIX_WITH_AWS_AUTH
#    include "nix/store/aws-creds.hh"
#    include "nix/store/s3-url.hh"
#    include "nix/util/url.hh"
#  endif

#  include <nlohmann/json.hpp>

/* This definition is undocumented but depended upon by all major browsers. */
extern "C" int
sandbox_init_with_parameters(const char * profile, uint64_t flags, const char * const parameters[], char ** errorbuf);

/* Darwin IPC structures and constants */
#  define IPCS_MAGIC 0x00000001
#  define IPCS_SHM_ITER 0x00000002
#  define IPCS_SEM_ITER 0x00000020
#  define IPCS_MSG_ITER 0x00000200
#  define IPCS_SHM_SYSCTL "kern.sysv.ipcs.shm"
#  define IPCS_MSG_SYSCTL "kern.sysv.ipcs.msg"
#  define IPCS_SEM_SYSCTL "kern.sysv.ipcs.sem"

struct IpcsCommand
{
    uint32_t ipcs_magic;
    uint32_t ipcs_op;
    uint32_t ipcs_cursor;
    uint32_t ipcs_datalen;
    void * ipcs_data;
};

namespace nix {

struct DarwinDerivationBuilder : DerivationBuilder, DerivationBuilderParams
{
    PathsInChroot pathsInChroot;

    /**
     * Whether full sandboxing is enabled. Note that macOS builds
     * always have *some* sandboxing (see sandbox-minimal.sb).
     */
    bool useSandbox;

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
     * Arguments passed to runChild().
     */
    struct RunChildArgs
    {
#  if NIX_WITH_AWS_AUTH
        std::optional<AwsCredentials> awsCredentials;
#  endif
    };

    DarwinDerivationBuilder(
        LocalStore & store,
        std::unique_ptr<DerivationBuilderCallbacks> miscMethods,
        DerivationBuilderParams params,
        bool useSandbox)
        : DerivationBuilderParams{std::move(params)}
        , store{store}
        , miscMethods{std::move(miscMethods)}
        , derivationType{drv.type()}
        , useSandbox{useSandbox}
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
        PathsInChroot result = defaultPathsInChroot;

        if (hasPrefix(store.storeDir, tmpDirInSandbox().native())) {
            throw Error("`sandbox-build-dir` must not contain the storeDir");
        }
        result[tmpDirInSandbox()] = {.source = tmpDir};

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

            result[i] = {i, true};
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
                            result[line] = {.source = line};
                        else
                            result[line.substr(0, p)] = {.source = line.substr(p + 1)};
                    }
                }
            }
        }

        return result;
    }

    void setBuildTmpDir()
    {
        tmpDir = topTmpDir / "build";
        createDir(tmpDir, 0700);
    }
    std::filesystem::path tmpDirInSandbox()
    {
        return "/build";
    }

    void prepareUser()
    {
        killSandbox(false);
    }

    void prepareSandbox()
    {
        pathsInChroot = getPathsInSandbox();
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

#  if NIX_WITH_AWS_AUTH
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
#  endif

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
        if (!buildUser)
            return;
        if (chown(path.c_str(), buildUser->getUID(), buildUser->getGID()) == -1)
            throw SysError("cannot change ownership of %1%", PathFmt(path));
    }

    void chownToBuilder(int fd, const std::filesystem::path & path)
    {
        if (!buildUser)
            return;
        if (fchown(fd, buildUser->getUID(), buildUser->getGID()) == -1)
            throw SysError("cannot change ownership of file %1%", PathFmt(path));
    }

    void writeBuilderFile(const std::string & name, std::string_view contents)
    {
        auto path = std::filesystem::path(tmpDir) / name;
        AutoCloseFD fd{openat(
            tmpDirFd.get(),
            name.c_str(),
            O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC | O_EXCL | O_NOFOLLOW,
            0666)};
        if (!fd)
            throw SysError("creating file %s", PathFmt(path));
        writeFile(fd, path, contents);
        chownToBuilder(fd.get(), path);
    }

    void initEnv()
    {
        env.clear();

        env["PATH"] = "/path-not-set";
        env["HOME"] = homeDir;
        env["NIX_STORE"] = store.storeDir;
        env["NIX_BUILD_CORES"] = fmt(
            "%d",
            settings.getLocalSettings().buildCores ? settings.getLocalSettings().buildCores
                                                    : settings.getDefaultCores());

        for (const auto & [name, info] : desugaredEnv.variables) {
            env[name] = info.prependBuildDirectory ? (tmpDirInSandbox() / info.value).string() : info.value;
        }

        for (const auto & [fileName, value] : desugaredEnv.extraFiles) {
            writeBuilderFile(fileName, rewriteStrings(value, inputRewrites));
        }

        env["NIX_BUILD_TOP"] = tmpDirInSandbox();
        env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = tmpDirInSandbox();
        env["PWD"] = tmpDirInSandbox();

        if (derivationType.isFixed())
            env["NIX_OUTPUT_CHECKED"] = "1";

        if (!derivationType.isSandboxed()) {
            auto & impureEnv = localSettings.impureEnv.get();
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

        env["NIX_LOG_FD"] = "2";
        env["TERM"] = "xterm-256color";
    }

    /**
     * No-op on Darwin: we do not use an actual chroot.
     */
    void enterChroot() {}

    /**
     * Change the current process's uid/gid to the build user, if any,
     * then apply the Darwin sandbox profile.
     */
    void setUser()
    {
        /* First, drop privileges to the build user (same as the base implementation). */
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

        /* This has to appear before import statements. */
        std::string sandboxProfile = "(version 1)\n";

        if (useSandbox) {

            /* Lots and lots and lots of file functions freak out if they can't stat their full ancestry */
            PathSet ancestry;

            /* We build the ancestry before adding all inputPaths to the store because we know they'll
               all have the same parents (the store), and there might be lots of inputs. This isn't
               particularly efficient... I doubt it'll be a bottleneck in practice */
            for (auto & i : pathsInChroot) {
                std::filesystem::path cur = i.first;
                while (cur != "/") {
                    cur = cur.parent_path();
                    ancestry.insert(cur.native());
                }
            }

            /* And we want the store in there regardless of how empty pathsInChroot. We include the innermost
               path component this time, since it's typically /nix/store and we care about that. */
            std::filesystem::path cur = store.storeDir;
            while (cur != "/") {
                ancestry.insert(cur.native());
                cur = cur.parent_path();
            }

            /* Add all our input paths to the chroot */
            for (auto & i : inputPaths) {
                auto p = store.printStorePath(i);
                pathsInChroot.insert_or_assign(p, ChrootPath{.source = p});
            }

            /* Violations will go to the syslog if you set this. Unfortunately the destination does not appear to be
             * configurable */
            if (store.config->getLocalSettings().darwinLogSandboxViolations) {
                sandboxProfile += "(deny default)\n";
            } else {
                sandboxProfile += "(deny default (with no-log))\n";
            }

            sandboxProfile +=
#  include "sandbox-defaults.sb"
                ;

            if (!derivationType.isSandboxed())
                sandboxProfile +=
#  include "sandbox-network.sb"
                    ;

            /* Add the output paths we'll use at build-time to the chroot */
            sandboxProfile += "(allow file-read* file-write* process-exec\n";
            for (auto & [_, path] : scratchOutputs)
                sandboxProfile += fmt("\t(subpath \"%s\")\n", store.printStorePath(path));

            sandboxProfile += ")\n";

            /* Our inputs (transitive dependencies and any impurities computed above)

               without file-write* allowed, access() incorrectly returns EPERM
             */
            sandboxProfile += "(allow file-read* file-write* process-exec\n";

            // We create multiple allow lists, to avoid exceeding a limit in the darwin sandbox interpreter.
            // See https://github.com/NixOS/nix/issues/4119
            // We split our allow groups approximately at half the actual limit, 1 << 16
            const size_t breakpoint = sandboxProfile.length() + (1 << 14);
            for (auto & i : pathsInChroot) {

                if (sandboxProfile.length() >= breakpoint) {
                    debug("Sandbox break: %d %d", sandboxProfile.length(), breakpoint);
                    sandboxProfile += ")\n(allow file-read* file-write* process-exec\n";
                }

                if (i.first != i.second.source)
                    throw Error(
                        "can't map %1% to %2%: mismatched impure paths not supported on Darwin",
                        PathFmt(i.first),
                        PathFmt(i.second.source));

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

            sandboxProfile += drvOptions.additionalSandboxProfile;
        } else
            sandboxProfile +=
#  include "sandbox-minimal.sb"
                ;

        debug("Generated sandbox profile:");
        debug(sandboxProfile);

        /* The tmpDir in scope points at the temporary build directory for our derivation. Some packages try different
           mechanisms to find temporary directories, so we want to open up a broader place for them to put their files,
           if needed. */
        std::filesystem::path globalTmpDir = canonPath(defaultTempDir().native(), true);

        /* They don't like trailing slashes on subpath directives */
        std::string globalTmpDirStr = globalTmpDir.native();
        while (!globalTmpDirStr.empty() && globalTmpDirStr.back() == '/')
            globalTmpDirStr.pop_back();

        if (getEnv("_NIX_TEST_NO_SANDBOX") != "1") {
            Strings sandboxArgs;
            sandboxArgs.push_back("_NIX_BUILD_TOP");
            sandboxArgs.push_back(tmpDir.native());
            sandboxArgs.push_back("_GLOBAL_TMP_DIR");
            sandboxArgs.push_back(globalTmpDirStr);
            if (drvOptions.allowLocalNetworking) {
                sandboxArgs.push_back("_ALLOW_LOCAL_NETWORKING");
                sandboxArgs.push_back("1");
            }
            char * sandbox_errbuf = nullptr;
            if (sandbox_init_with_parameters(
                    sandboxProfile.c_str(), 0, stringsToCharPtrs(sandboxArgs).data(), &sandbox_errbuf)) {
                writeFull(
                    STDERR_FILENO,
                    fmt("failed to configure sandbox: %s\n", sandbox_errbuf ? sandbox_errbuf : "(null)"));
                _exit(1);
            }
        }
    }

    /**
     * Execute the builder using `posix_spawn` with platform-specific CPU affinity.
     */
    void execBuilder(const Strings & args, const Strings & envStrs)
    {
        posix_spawnattr_t attrp;

        if (posix_spawnattr_init(&attrp))
            throw SysError("failed to initialize builder");

        if (posix_spawnattr_setflags(&attrp, POSIX_SPAWN_SETEXEC))
            throw SysError("failed to initialize builder");

        if (drv.platform == "aarch64-darwin") {
            // Unset kern.curproc_arch_affinity so we can escape Rosetta
            int affinity = 0;
            sysctlbyname("kern.curproc_arch_affinity", NULL, NULL, &affinity, sizeof(affinity));

            cpu_type_t cpu = CPU_TYPE_ARM64;
            posix_spawnattr_setbinpref_np(&attrp, 1, &cpu, NULL);
        } else if (drv.platform == "x86_64-darwin") {
            cpu_type_t cpu = CPU_TYPE_X86_64;
            posix_spawnattr_setbinpref_np(&attrp, 1, &cpu, NULL);
        }

        posix_spawn(
            NULL, drv.builder.c_str(), NULL, &attrp, stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());
    }

    /**
     * Run the builder's process. Called from within the child process
     * started by `startChild()`.
     */
    void runChild(RunChildArgs args)
    {
        /* Warning: in the child we should absolutely not make any SQLite calls! */

        bool sendException = true;

        try { /* child */

            commonChildInit();

            /* Make the contents of netrc and the CA certificate bundle
               available to builtin:fetchurl (which may run under a
               different uid and/or in a sandbox). */
            BuiltinBuilderContext ctx{
                .drv = drv,
                .hashedMirrors = settings.getLocalSettings().hashedMirrors,
                .tmpDirInSandbox = tmpDirInSandbox(),
#  if NIX_WITH_AWS_AUTH
                .awsCredentials = args.awsCredentials,
#  endif
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

            Strings builderArgs;
            builderArgs.push_back(std::string(baseNameOf(drv.builder)));

            for (auto & i : drv.args)
                builderArgs.push_back(rewriteStrings(i, inputRewrites));

            Strings envStrs;
            for (auto & i : env)
                envStrs.push_back(rewriteStrings(i.first + "=" + i.second, inputRewrites));

            execBuilder(builderArgs, envStrs);

            throw SysError("executing '%1%'", drv.builder);

        } catch (...) {
            handleChildExceptionDarwin(sendException);
            _exit(1);
        }
    }

    void startChild()
    {
        RunChildArgs args{
#  if NIX_WITH_AWS_AUTH
            .awsCredentials = preResolveAwsCredentials(),
#  endif
        };

        pid = startProcess([this, args = std::move(args)]() {
            openSlave();
            runChild(std::move(args));
        });
    }

    void killSandbox(bool getStats)
    {
        if (buildUser) {
            auto uid = buildUser->getUID();
            assert(uid != 0);
            killUser(uid);
            cleanupSysVIPCForUser(uid);
        }
    }

    /**
     * Cleans up all System V IPC objects owned by the specified user.
     *
     * On Darwin, IPC objects (shared memory segments, message queues, and semaphores)
     * can persist after the build user's processes are killed, since there are no IPC namespaces
     * like on Linux. This can exhaust kernel IPC limits over time.
     *
     * Uses sysctl to enumerate and remove all IPC objects owned by the given UID.
     */
    void cleanupSysVIPCForUser(uid_t uid)
    {
        struct IpcsCommand ic;
        size_t ic_size = sizeof(ic);
        // IPC ids to cleanup
        std::vector<int> shm_ids, msg_ids, sem_ids;

        {
            struct shmid_ds shm_ds;
            ic.ipcs_magic = IPCS_MAGIC;
            ic.ipcs_op = IPCS_SHM_ITER;
            ic.ipcs_cursor = 0;
            ic.ipcs_data = &shm_ds;
            ic.ipcs_datalen = sizeof(shm_ds);

            while (true) {
                memset(&shm_ds, 0, sizeof(shm_ds));

                if (sysctlbyname(IPCS_SHM_SYSCTL, &ic, &ic_size, &ic, ic_size) != 0) {
                    break;
                }

                if (shm_ds.shm_perm.uid == uid) {
                    int shmid = shmget(shm_ds.shm_perm._key, 0, 0);
                    if (shmid != -1) {
                        shm_ids.push_back(shmid);
                    }
                }
            }
        }

        for (auto id : shm_ids) {
            if (shmctl(id, IPC_RMID, NULL) == 0)
                debug("removed shared memory segment with shmid %d", id);
        }

        {
            struct msqid_ds msg_ds;
            ic.ipcs_magic = IPCS_MAGIC;
            ic.ipcs_op = IPCS_MSG_ITER;
            ic.ipcs_cursor = 0;
            ic.ipcs_data = &msg_ds;
            ic.ipcs_datalen = sizeof(msg_ds);

            while (true) {
                memset(&msg_ds, 0, sizeof(msg_ds));

                if (sysctlbyname(IPCS_MSG_SYSCTL, &ic, &ic_size, &ic, ic_size) != 0) {
                    break;
                }

                if (msg_ds.msg_perm.uid == uid) {
                    int msgid = msgget(msg_ds.msg_perm._key, 0);
                    if (msgid != -1) {
                        msg_ids.push_back(msgid);
                    }
                }
            }
        }

        for (auto id : msg_ids) {
            if (msgctl(id, IPC_RMID, NULL) == 0)
                debug("removed message queue with msgid %d", id);
        }

        {
            struct semid_ds sem_ds;
            ic.ipcs_magic = IPCS_MAGIC;
            ic.ipcs_op = IPCS_SEM_ITER;
            ic.ipcs_cursor = 0;
            ic.ipcs_data = &sem_ds;
            ic.ipcs_datalen = sizeof(sem_ds);

            while (true) {
                memset(&sem_ds, 0, sizeof(sem_ds));

                if (sysctlbyname(IPCS_SEM_SYSCTL, &ic, &ic_size, &ic, ic_size) != 0) {
                    break;
                }

                if (sem_ds.sem_perm.uid == uid) {
                    int semid = semget(sem_ds.sem_perm._key, 0, 0);
                    if (semid != -1) {
                        sem_ids.push_back(semid);
                    }
                }
            }
        }

        for (auto id : sem_ids) {
            if (semctl(id, 0, IPC_RMID) == 0)
                debug("removed semaphore with semid %d", id);
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
#  if HAVE_STATVFS
        {
            uint64_t required = 8ULL * 1024 * 1024;
            struct statvfs st;
            if (statvfs(store.config->realStoreDir.get().c_str(), &st) == 0
                && (uint64_t) st.f_bavail * st.f_bsize < required)
                diskFull = true;
            if (statvfs(tmpDir.c_str(), &st) == 0 && (uint64_t) st.f_bavail * st.f_bsize < required)
                diskFull = true;
        }
#  endif
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
        } else {
            if (grantpt(builderOut.get()))
                throw SysError("granting access to pseudoterminal slave");
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

const std::filesystem::path DarwinDerivationBuilder::homeDir = "/homeless-shelter";

DerivationBuilderUnique makeDarwinDerivationBuilder(
    LocalStore & store,
    std::unique_ptr<DerivationBuilderCallbacks> miscMethods,
    DerivationBuilderParams params,
    bool useSandbox)
{
    return DerivationBuilderUnique(
        new DarwinDerivationBuilder(store, std::move(miscMethods), std::move(params), useSandbox));
}

} // namespace nix

#endif
