#include "generic-unix-derivation-builder.hh"
#include "derivation-builder-common.hh"
#include "nix/store/build/derivation-builder.hh"
#include "nix/util/file-system.hh"
#include "nix/store/local-store.hh"
#include "nix/util/processes.hh"
#include "nix/store/builtins.hh"
#include "nix/store/build/child.hh"
#include "nix/store/user-lock.hh"
#include "nix/store/globals.hh"
#include "nix/store/restricted-store.hh"

#include <fcntl.h>
#include <unistd.h>
#include <sys/resource.h>

#if NIX_WITH_AWS_AUTH
#  include "nix/store/aws-creds.hh"
#endif

namespace nix {

struct GenericUnixDerivationBuilder : DerivationBuilder, DerivationBuilderParams
{
    Pid pid;

    LocalStore & store;

    const LocalSettings & localSettings = store.config->getLocalSettings();

    std::unique_ptr<DerivationBuilderCallbacks> miscMethods;

    std::unique_ptr<UserLock> buildUser;

    std::filesystem::path tmpDir;

    std::filesystem::path topTmpDir;

    const DerivationType derivationType;

    std::map<StorePath, StorePath> redirectedOutputs;

    OutputPathMap scratchOutputs;

    AutoCloseFD daemonSocket;
    std::thread daemonThread;
    std::vector<std::thread> daemonWorkerThreads;

    GenericUnixDerivationBuilder(
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

    std::filesystem::path tmpDirInSandbox()
    {
        assert(!topTmpDir.empty());
        return topTmpDir;
    }

    void addDependencyImpl(const StorePath & path) override
    {
        addedPaths.insert(path);
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

    void cleanupBuild(bool force)
    {
        nix::cleanupBuildCore(force, store, redirectedOutputs, drv, topTmpDir, tmpDir);
    }

    std::optional<Descriptor> startBuild() override
    {
        if (useBuildUsers(localSettings)) {
            if (!buildUser)
                buildUser = acquireUserLock(settings.nixStateDir, localSettings, 1, false);

            if (!buildUser)
                return std::nullopt;
        }

        killSandbox(false);

        auto buildDir = store.config->getBuildDir();

        createDirs(buildDir);

        if (buildUser)
            checkNotWorldWritable(buildDir);

        topTmpDir = createTempDir(buildDir, "nix", 0700);
        tmpDir = topTmpDir;
        assert(!tmpDir.empty());

        AutoCloseFD tmpDirFd{open(tmpDir.c_str(), O_RDONLY | O_NOFOLLOW | O_DIRECTORY)};
        if (!tmpDirFd)
            throw SysError("failed to open the build temporary directory descriptor %1%", PathFmt(tmpDir));

        nix::chownToBuilder(buildUser.get(), tmpDirFd.get(), tmpDir);

        StringMap inputRewrites;
        std::tie(scratchOutputs, inputRewrites, redirectedOutputs) =
            nix::computeScratchOutputs(store, *this, /* needsHashRewrite= */ true);

        auto env = nix::initEnv(
            store.storeDir,
            *this,
            inputRewrites,
            derivationType,
            localSettings,
            tmpDirInSandbox(),
            buildUser.get(),
            tmpDir,
            tmpDirFd.get());

        if (drvOptions.useUidRange(drv))
            throw Error("feature 'uid-range' is not supported on this platform");

        if (pathExists(homeDir))
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

        /* Start child */
        {
#if NIX_WITH_AWS_AUTH
            auto awsCredentials = nix::preResolveAwsCredentials(drv);
#endif

            pid = startProcess([this, &env, &inputRewrites
#if NIX_WITH_AWS_AUTH
                                ,
                                awsCredentials
#endif
            ]() {
                nix::setupPTYSlave(builderOut.get());

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

                    if (chdir(tmpDirInSandbox().c_str()) == -1)
                        throw SysError("changing into %1%", PathFmt(tmpDir));

                    unix::closeExtraFDs();

                    struct rlimit limit = {0, RLIM_INFINITY};
                    setrlimit(RLIMIT_CORE, &limit);

                    if (buildUser)
                        nix::dropPrivileges(*buildUser);

                    writeFull(STDERR_FILENO, std::string("\2\n"));

                    sendException = false;

                    if (drv.isBuiltin())
                        nix::runBuiltinBuilder(ctx, drv, scratchOutputs, store);

                    nix::execBuilder(drv, inputRewrites, env);

                } catch (...) {
                    handleChildException(sendException);
                    _exit(1);
                }
            });
        }

        pid.setSeparatePG(true);

        nix::processSandboxSetupMessages(builderOut, pid, store, drvPath);

        return builderOut.get();
    }

    SingleDrvOutputs unprepareBuild() override
    {
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

        auto builtOutputs = nix::registerOutputs(
            store,
            localSettings,
            *this,
            addedPaths,
            scratchOutputs,
            buildUser.get(),
            tmpDir,
            [this](const std::string & p) { return store.toRealPath(p); });

        cleanupBuild(true);

        return builtOutputs;
    }
};

DerivationBuilderUnique makeGenericUnixDerivationBuilder(
    LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
{
    return DerivationBuilderUnique(new GenericUnixDerivationBuilder(store, std::move(miscMethods), std::move(params)));
}

} // namespace nix
