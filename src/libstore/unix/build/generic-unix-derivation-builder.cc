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

struct GenericUnixDerivationBuilder : DerivationBuilder, DerivationBuilderParams, BuilderCore
{
    GenericUnixDerivationBuilder(
        LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
        : DerivationBuilder{params.inputPaths}
        , DerivationBuilderParams{std::move(params)}
        , BuilderCore{store, std::move(miscMethods), drv}
    {
    }

    void cleanupOnDestruction() noexcept override
    {
        BuilderCore::cleanupOnDestruction(*this);
        try {
            cleanupBuild(false);
        } catch (...) {
            ignoreExceptionInDestructor();
        }
    }

    std::filesystem::path tmpDirInSandbox()
    {
        assert(!topTmpDir.empty());
        return topTmpDir;
    }

    void killSandbox(bool getStats)
    {
        killSandboxBase(getStats);
    }

    bool killChild() override
    {
        return BuilderCore::killChild(*miscMethods);
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
            daemon.start(store, *this, env, tmpDir, tmpDirInSandbox(), buildUser.get());

        nix::logBuilderInfo(drv);

        miscMethods->openLogFile();

        nix::setupPTYMaster(builderOut, buildUser.get());

        buildResult.startTime = time(0);

        /* Start child */
        {
#if NIX_WITH_AWS_AUTH
            auto awsCredentials = nix::preResolveAwsCredentials(drv);
#endif

            struct RunChildArgs
            {
                StringMap env;
                StringMap inputRewrites;
#if NIX_WITH_AWS_AUTH
                std::optional<AwsCredentials> awsCredentials;
#endif
            };

            RunChildArgs args{
                .env = std::move(env),
                .inputRewrites = std::move(inputRewrites),
#if NIX_WITH_AWS_AUTH
                .awsCredentials = std::move(awsCredentials),
#endif
            };

            pid = startProcess([this, args = std::move(args)]() mutable {
                auto & env = args.env;
                auto & inputRewrites = args.inputRewrites;
#if NIX_WITH_AWS_AUTH
                auto & awsCredentials = args.awsCredentials;
#endif

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

                    /* Make sure the builder inherits a predictable umask. It must not be group-writable, since
                     * registerOutputs rejects those as defense-in-depth. */
                    umask(0022);

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
        return unprepareBuildCommon(
            *this,
            builderOut,
            addedPaths,
            [this](bool s) { killSandbox(s); },
            [this](bool f) { cleanupBuild(f); },
            [this](const std::filesystem::path & p) { return store.toRealPath(store.parseStorePath(p.native())); });
    }
};

DerivationBuilderUnique makeGenericUnixDerivationBuilder(
    LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
{
    return DerivationBuilderUnique(new GenericUnixDerivationBuilder(store, std::move(miscMethods), std::move(params)));
}

} // namespace nix
