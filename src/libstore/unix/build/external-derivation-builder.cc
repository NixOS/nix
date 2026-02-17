#include "external-derivation-builder.hh"
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
#include "build/derivation-check.hh"
#include "store-config-private.hh"

#include <sys/un.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#ifdef __linux__
#  include <sys/prctl.h>
#endif

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

#include <nlohmann/json.hpp>

namespace nix {

struct ExternalDerivationBuilder : DerivationBuilder, DerivationBuilderParams
{
    ExternalBuilder externalBuilder;

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
     * The file descriptor of the temporary directory.
     */
    AutoCloseFD tmpDirFd;

    /**
     * The sort of derivation we are building.
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

    ExternalDerivationBuilder(
        LocalStore & store,
        std::unique_ptr<DerivationBuilderCallbacks> miscMethods,
        DerivationBuilderParams params,
        ExternalBuilder externalBuilder)
        : DerivationBuilderParams{std::move(params)}
        , externalBuilder{std::move(externalBuilder)}
        , store{store}
        , miscMethods{std::move(miscMethods)}
        , derivationType{drv.type()}
    {
        experimentalFeatureSettings.require(Xp::ExternalBuilders);
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

    void startChild()
    {
        if (drvOptions.getRequiredSystemFeatures(drv).count("recursive-nix"))
            throw Error("'recursive-nix' is not supported yet by external derivation builders");

        auto json = nlohmann::json::object();

        json.emplace("version", 1);
        json.emplace("builder", drv.builder);
        {
            auto l = nlohmann::json::array();
            for (auto & i : drv.args)
                l.push_back(rewriteStrings(i, inputRewrites));
            json.emplace("args", std::move(l));
        }
        {
            auto j = nlohmann::json::object();
            for (auto & [name, value] : env)
                j.emplace(name, rewriteStrings(value, inputRewrites));
            json.emplace("env", std::move(j));
        }
        json.emplace("topTmpDir", topTmpDir.native());
        json.emplace("tmpDir", tmpDir.native());
        json.emplace("tmpDirInSandbox", tmpDirInSandbox().native());
        json.emplace("storeDir", store.storeDir);
        json.emplace("realStoreDir", store.config->realStoreDir.get());
        json.emplace("system", drv.platform);
        {
            auto l = nlohmann::json::array();
            for (auto & i : inputPaths)
                l.push_back(store.printStorePath(i));
            json.emplace("inputPaths", std::move(l));
        }
        {
            auto l = nlohmann::json::object();
            for (auto & i : scratchOutputs)
                l.emplace(i.first, store.printStorePath(i.second));
            json.emplace("outputs", std::move(l));
        }

        // TODO(cole-h): writing this to stdin is too much effort right now, if we want to revisit
        // that, see this comment by Eelco about how to make it not suck:
        // https://github.com/DeterminateSystems/nix-src/pull/141#discussion_r2205493257
        auto jsonFile = std::filesystem::path{topTmpDir} / "build.json";
        writeFile(jsonFile, json.dump());

        pid = startProcess([&]() {
            openSlave();
            try {
                commonChildInit();

                Strings args = {externalBuilder.program};

                if (!externalBuilder.args.empty()) {
                    args.insert(args.end(), externalBuilder.args.begin(), externalBuilder.args.end());
                }

                args.insert(args.end(), jsonFile);

                if (chdir(tmpDir.c_str()) == -1)
                    throw SysError("changing into %1%", PathFmt(tmpDir));

                chownToBuilder(topTmpDir);

                setUser();

                debug("executing external builder: %s", concatStringsSep(" ", args));
                execv(externalBuilder.program.c_str(), stringsToCharPtrs(args).data());

                throw SysError("executing %s", PathFmt(externalBuilder.program));
            } catch (...) {
                handleChildExceptionExternal(true);
                _exit(1);
            }
        });
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

        tmpDirFd = AutoCloseFD{open(tmpDir.c_str(), O_RDONLY | O_NOFOLLOW | O_DIRECTORY)};
        if (!tmpDirFd)
            throw SysError("failed to open the build temporary directory descriptor %1%", PathFmt(tmpDir));

        chownToBuilder(tmpDirFd.get(), tmpDir);

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
#ifdef __APPLE__
        else {
            if (grantpt(builderOut.get()))
                throw SysError("granting access to pseudoterminal slave");
        }
#endif

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

const std::filesystem::path ExternalDerivationBuilder::homeDir = "/homeless-shelter";

DerivationBuilderUnique makeExternalDerivationBuilder(
    LocalStore & store,
    std::unique_ptr<DerivationBuilderCallbacks> miscMethods,
    DerivationBuilderParams params,
    const ExternalBuilder & handler)
{
    return DerivationBuilderUnique(
        new ExternalDerivationBuilder(store, std::move(miscMethods), std::move(params), handler));
}

} // namespace nix
