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
#  include "nix/store/build/child.hh"
#  include "nix/store/restricted-store.hh"
#  include "nix/store/user-lock.hh"
#  include "nix/store/globals.hh"
#  include "store-config-private.hh"

#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/resource.h>

#  include "nix/util/strings.hh"
#  include "nix/util/signals.hh"

#  if NIX_WITH_AWS_AUTH
#    include "nix/store/aws-creds.hh"
#  endif

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

    bool useSandbox;

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

            /* Clean up System V IPC objects owned by this user. */
            struct IpcsCommand ic;
            size_t ic_size = sizeof(ic);
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

        /* Compute paths to expose in the sandbox */
        {
            pathsInChroot = defaultPathsInChroot;

            if (hasPrefix(store.storeDir, tmpDirInSandbox().native())) {
                throw Error("`sandbox-build-dir` must not contain the storeDir");
            }
            pathsInChroot[tmpDirInSandbox()] = {.source = tmpDir};

            nix::checkAndAddImpurePaths(
                pathsInChroot, drvOptions, store, drvPath, localSettings.allowedImpureHostPrefixes);

            if (localSettings.preBuildHook != "") {
                printMsg(lvlChatty, "executing pre-build hook '%1%'", localSettings.preBuildHook);
                auto lines = runProgram(localSettings.preBuildHook, false, Strings({store.printStorePath(drvPath)}));
                nix::parsePreBuildHook(pathsInChroot, lines);
            }
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

        nix::setupPTYMaster(builderOut, buildUser.get(), true);

        buildResult.startTime = time(0);

        /* Start the child process */
        {
#  if NIX_WITH_AWS_AUTH
            auto awsCredentials = nix::preResolveAwsCredentials(drv);
#  endif

            pid = startProcess([this
#  if NIX_WITH_AWS_AUTH
                                ,
                                awsCredentials
#  endif
            ]() {
                nix::setupPTYSlave(builderOut.get());

                /* Warning: in the child we should absolutely not make any SQLite calls! */

                bool sendException = true;

                try { /* child */

                    commonChildInit();

                    BuiltinBuilderContext ctx{
                        .drv = drv,
                        .hashedMirrors = settings.getLocalSettings().hashedMirrors,
                        .tmpDirInSandbox = tmpDirInSandbox(),
#  if NIX_WITH_AWS_AUTH
                        .awsCredentials = awsCredentials,
#  endif
                    };

                    nix::setupBuiltinFetchurlContext(ctx, drv);

                    /* enterChroot() is a no-op on Darwin */

                    if (chdir(tmpDirInSandbox().c_str()) == -1)
                        throw SysError("changing into %1%", PathFmt(tmpDir));

                    /* Close all other file descriptors. */
                    unix::closeExtraFDs();

                    /* Disable core dumps by default. */
                    struct rlimit limit = {0, RLIM_INFINITY};
                    setrlimit(RLIMIT_CORE, &limit);

                    // FIXME: set other limits to deterministic values?

                    /* Drop privileges to the build user and apply Darwin sandbox profile */
                    {
                        if (buildUser)
                            nix::dropPrivileges(*buildUser);

                        /* This has to appear before import statements. */
                        std::string sandboxProfile = "(version 1)\n";

                        if (useSandbox) {

                            /* Lots and lots and lots of file functions freak out if they can't stat their full ancestry
                             */
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

                            /* And we want the store in there regardless of how empty pathsInChroot. We include the
                               innermost path component this time, since it's typically /nix/store and we care about
                               that. */
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

                            /* Violations will go to the syslog if you set this. Unfortunately the destination does not
                             * appear to be configurable */
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

                            // We create multiple allow lists, to avoid exceeding a limit in the darwin sandbox
                            // interpreter. See https://github.com/NixOS/nix/issues/4119 We split our allow groups
                            // approximately at half the actual limit, 1 << 16
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

                        /* The tmpDir in scope points at the temporary build directory for our derivation. Some packages
                           try different mechanisms to find temporary directories, so we want to open up a broader place
                           for them to put their files, if needed. */
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
                                    sandboxProfile.c_str(),
                                    0,
                                    stringsToCharPtrs(sandboxArgs).data(),
                                    &sandbox_errbuf)) {
                                writeFull(
                                    STDERR_FILENO,
                                    fmt("failed to configure sandbox: %s\n",
                                        sandbox_errbuf ? sandbox_errbuf : "(null)"));
                                _exit(1);
                            }
                        }
                    }

                    /* Indicate that we managed to set up the build environment. */
                    writeFull(STDERR_FILENO, std::string("\2\n"));

                    sendException = false;

                    if (drv.isBuiltin())
                        nix::runBuiltinBuilder(ctx, drv, scratchOutputs, store);

                    /* It's not a builtin builder, so execute the program. */

                    Strings builderArgs;
                    builderArgs.push_back(std::string(baseNameOf(drv.builder)));

                    for (auto & i : drv.args)
                        builderArgs.push_back(rewriteStrings(i, inputRewrites));

                    Strings envStrs;
                    for (auto & i : env)
                        envStrs.push_back(rewriteStrings(i.first + "=" + i.second, inputRewrites));

                    /* Execute the builder using posix_spawn with platform-specific CPU affinity */
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
                            NULL,
                            drv.builder.c_str(),
                            NULL,
                            &attrp,
                            stringsToCharPtrs(builderArgs).data(),
                            stringsToCharPtrs(envStrs).data());
                    }

                    throw SysError("executing '%1%'", drv.builder);

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

        outputRewrites.clear();
        auto builtOutputs = nix::registerOutputs(
            store, localSettings, *this, addedPaths, scratchOutputs,
            outputRewrites, buildUser.get(), tmpDir,
            [this](const std::string & p) -> std::filesystem::path { return store.toRealPath(p); });

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
