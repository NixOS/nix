#include "linux-derivation-builder.hh"
#include "linux-derivation-builder-common.hh"
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
#include "nix/store/personality.hh"

#include <fcntl.h>
#include <unistd.h>
#include <sys/resource.h>

#if NIX_WITH_AWS_AUTH
#  include "nix/store/aws-creds.hh"
#endif

namespace nix {

#  if HAVE_LANDLOCK && defined(LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET)

#    define DO_LANDLOCK 1

/* We are using LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET on best-effort basis. There are no glibc wrappers for now. */

static int landlockCreateRuleset(const ::landlock_ruleset_attr * attr, std::size_t size, std::uint32_t flags)
{
    return ::syscall(__NR_landlock_create_ruleset, attr, size, flags);
}

static int landlockRestrictSelf(Descriptor rulesetFd, std::uint32_t flags)
{
    return ::syscall(__NR_landlock_restrict_self, rulesetFd, flags);
}

static int getLandlockAbiVersion()
{
    int abiVersion = landlockCreateRuleset(nullptr, 0, LANDLOCK_CREATE_RULESET_VERSION);
    return abiVersion;
}

static void setupLandlock()
{
    bool landlockSupportsScopeAbstractUnixSocket = []() {
        int abiVersion = getLandlockAbiVersion();
        if (abiVersion >= 6)
            /* All good, we can use LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET. See
               https://docs.kernel.org/userspace-api/landlock.html#abstract-unix-socket-abi-6 */
            return true;

        if (abiVersion == -1) {
            debug("landlock is not available");
            return false;
        }

        debug("landlock version %d does not support LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET", abiVersion);
        return false;
    }();

    /* Bail out early if landlock is not enabled or LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET wouldn't work.
       TODO: Consider adding more landlock rules for filesystem access as defense-in-depth on top. */
    if (!landlockSupportsScopeAbstractUnixSocket)
        return;

    ::landlock_ruleset_attr attr = {
        /* This prevents multiple FODs from communicating with each other
           via abstract sockets. Note that cooperating processes outside the
           sandbox can still connect to an abstract socket created by the FOD. To
           mitigate that issue entirely we'd still need network namespaces. */
        .scoped = LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET,
    };

    /* This better not fail - if the kernel reports a new enough ABI version we
       should treat any errors as fatal from now on. */
    AutoCloseFD rulesetFd = landlockCreateRuleset(&attr, sizeof(attr), 0);
    if (!rulesetFd)
        throw SysError("failed to create a landlock ruleset");

    if (landlockRestrictSelf(rulesetFd.get(), 0) == -1)
        throw SysError("failed to apply landlock");

    debug("applied landlock sandboxing");
}

#  else

#    define DO_LANDLOCK 0

#  endif

struct LinuxDerivationBuilder : DerivationBuilder, DerivationBuilderParams, BuilderCore
{
    LinuxDerivationBuilder(
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

                    /* Set the NO_NEW_PRIVS before doing seccomp/landlock setup.
                       landlock_restrict_self requires either NO_NEW_PRIVS or CAP_SYS_ADMIN.
                       With user namespaces we do get CAP_SYS_ADMIN. */
                    if (!localSettings.allowNewPrivileges)
                        if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1)
                            throw SysError("failed to set PR_SET_NO_NEW_PRIVS");

                    setupSeccomp(localSettings);

#  if DO_LANDLOCK
                    try {
                        setupLandlock();
                    } catch (SysError & e) {
                        if (e.errNo != EPERM)
                            throw;
                        /* If allowNewPrivileges is true and we don't have CAP_SYS_ADMIN
                           this code path might be hit. */
                        warn("setting up landlock: %s", e.message());
                    }
#  endif

                    linux::setPersonality({
                        .system = drv.platform,
                        .impersonateLinux26 = localSettings.impersonateLinux26,
                    });

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

DerivationBuilderUnique makeLinuxDerivationBuilder(
    LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
{
    return DerivationBuilderUnique(new LinuxDerivationBuilder(store, std::move(miscMethods), std::move(params)));
}

} // namespace nix

#  undef DO_LANDLOCK

#endif
