#include "external-derivation-builder.hh"
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
#include "store-config-private.hh"

#include <fcntl.h>
#include <unistd.h>
#include <sys/resource.h>

#include "nix/util/strings.hh"
#include "nix/util/signals.hh"

#include <nlohmann/json.hpp>

namespace nix {

struct ExternalDerivationBuilder : DerivationBuilder, DerivationBuilderParams
{
    ExternalBuilder externalBuilder;

    Pid pid;

    LocalStore & store;

    const LocalSettings & localSettings = store.config->getLocalSettings();

    std::unique_ptr<DerivationBuilderCallbacks> miscMethods;

    std::unique_ptr<UserLock> buildUser;

    std::filesystem::path tmpDir;

    std::filesystem::path topTmpDir;

    AutoCloseFD tmpDirFd;

    const DerivationType derivationType;

    StringMap env;

    StringMap inputRewrites, outputRewrites;
    std::map<StorePath, StorePath> redirectedOutputs;

    OutputPathMap scratchOutputs;

    static const std::filesystem::path homeDir;

    AutoCloseFD daemonSocket;
    std::thread daemonThread;
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
        return true;
    }

    std::filesystem::path tmpDirInSandbox()
    {
        return "/build";
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

        if (drvOptions.useUidRange(drv))
            throw Error("feature 'uid-range' is not supported on this platform");

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

        nix::setupPTYMaster(builderOut, buildUser.get(),
#ifdef __APPLE__
            true
#else
            false
#endif
        );

        buildResult.startTime = time(0);

        /* Start child */
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
                nix::setupPTYSlave(builderOut.get());

                try {
                    commonChildInit();

                    Strings args = {externalBuilder.program};

                    if (!externalBuilder.args.empty()) {
                        args.insert(args.end(), externalBuilder.args.begin(), externalBuilder.args.end());
                    }

                    args.insert(args.end(), jsonFile);

                    if (chdir(tmpDir.c_str()) == -1)
                        throw SysError("changing into %1%", PathFmt(tmpDir));

                    nix::chownToBuilder(buildUser.get(), topTmpDir);

                    if (buildUser)
                        nix::dropPrivileges(*buildUser);

                    debug("executing external builder: %s", concatStringsSep(" ", args));
                    execv(externalBuilder.program.c_str(), stringsToCharPtrs(args).data());

                    throw SysError("executing %s", PathFmt(externalBuilder.program));
                } catch (...) {
                    handleChildException(true);
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

        outputRewrites.clear();
        auto builtOutputs = nix::registerOutputs(
            store, localSettings, *this, addedPaths, scratchOutputs,
            outputRewrites, buildUser.get(), tmpDir,
            [this](const std::string & p) { return store.toRealPath(p); });

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
