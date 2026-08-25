#include "nix/store/build/derivation-builder.hh"
#include "nix/util/configuration.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/file-system.hh"
#include "nix/store/local-store.hh"
#include "nix/util/processes.hh"
#include "nix/store/builtins.hh"
#include "nix/util/util.hh"
#include "nix/store/build/child.hh"
#include "nix/util/unix-domain-socket.hh"
#include "nix/store/restricted-store.hh"
#include "nix/store/user-lock.hh"
#include "nix/store/globals.hh"
#include "nix/store/build/derivation-env-desugar.hh"
#include "nix/util/terminal.hh"
#include "nix/store/filetransfer.hh"

#include <sys/un.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#ifdef __linux__
#  include <sys/prctl.h>
#  include "nix/util/linux-namespaces.hh"
#endif

#include "store-config-private.hh"

#if HAVE_STATVFS
#  include <sys/statvfs.h>
#endif

#include <pwd.h>
#include <grp.h>
#include <iostream>
#include <list>
#include <atomic>

#include "nix/util/strings.hh"
#include "nix/util/signals.hh"

#include "store-config-private.hh"
#include "build/derivation-check.hh"

#include "unix-derivation-builder-impl.hh"

#ifdef __linux__
#  include "chroot-linux-derivation-builder.hh"
#endif

#ifdef __FreeBSD__
#  include "chroot-freebsd-derivation-builder.hh"
#endif

#ifdef __APPLE__
#  include "darwin-derivation-builder.hh"
#endif

#if NIX_WITH_AWS_AUTH
#  include "nix/store/aws-creds.hh"
#  include "nix/store/s3-url.hh"
#  include "nix/util/url.hh"
#endif

namespace nix {

void preserveDeathSignal(fun<void()> setCredentials)
{
#ifdef __linux__
    /* Record the old parent pid. This is to avoid a race in case the parent
       gets killed after setuid, but before we restored the death signal. It is
       zero if the parent isn't visible inside the PID namespace.
       See: https://stackoverflow.com/questions/284325/how-to-make-child-process-die-after-parent-exits */
    auto parentPid = getppid();

    int oldDeathSignal;
    if (prctl(PR_GET_PDEATHSIG, &oldDeathSignal) == -1)
        throw SysError("getting death signal");

    setCredentials(); /* Invoke the callback that does setuid etc. */

    /* Set the old death signal. SIGKILL is set by default in startProcess,
       but it gets cleared after setuid. Without this we end up with runaway
       build processes if we get killed. */
    if (prctl(PR_SET_PDEATHSIG, oldDeathSignal) == -1)
        throw SysError("setting death signal");

    /* The parent got killed and we got reparented. Commit seppuku. This check
       doesn't help much with PID namespaces, but it's still useful without
       sandboxing. */
    if (oldDeathSignal && getppid() != parentPid)
        raise(oldDeathSignal);
#else
    setCredentials(); /* Just call the function on non-Linux. */
#endif
}

void UnixDerivationBuilderImpl::anchor() {}

const std::filesystem::path UnixDerivationBuilderImpl::homeDir = "/homeless-shelter";

void UnixDerivationBuilderImpl::killSandbox(bool getStats)
{
    if (buildUser) {
        auto uid = buildUser->getUID();
        assert(uid != 0);
        killUser(uid);
    }
}

bool UnixDerivationBuilderImpl::killChild()
{
    bool ret = pid != unix::INVALID_PID;
    if (ret) {
        /* If we're using a build user, then there is a tricky race
           condition: if we kill the build user before the child has
           done its setuid() to the build user uid, then it won't be
           killed, and we'll potentially lock up in pid.wait().  So
           also send a conventional kill to the child. */
        ::kill(-pid, SIGKILL); /* ignore the result */

        killSandbox(true);

        pid.wait();

        miscMethods->childTerminated();
    }
    return ret;
}

SingleDrvOutputs UnixDerivationBuilderImpl::unprepareBuild()
{
    /* Since we got an EOF on the logger pipe, the builder is presumed
       to have terminated.  In fact, the builder could also have
       simply have closed its end of the pipe, so just to be sure,
       kill it. */
    int status = pid.kill();

    debug("builder process for '%s' finished", store.printStorePath(drvPath));

    buildResult.timesBuilt++;
    buildResult.stopTime = time(nullptr);

    /* So the child is gone now. */
    miscMethods->childTerminated();

    /* Close the read side of the logger pipe. */
    builderOut.close();

    /* Close the log file. */
    miscMethods->closeLogFile();

    /* When running under a build user, make sure that all processes
       running under that uid are gone.  This is to prevent a
       malicious user from leaving behind a process that keeps files
       open and modifies them after they have been chown'ed to
       root. */
    killSandbox(true);

    /* Terminate the recursive Nix daemons. */
    stopDaemon();

    if (buildResult.cpuUser && buildResult.cpuSystem) {
        debug(
            "builder for '%s' terminated with status %d, user CPU %.3fs, system CPU %.3fs",
            store.printStorePath(drvPath),
            status,
            ((double) buildResult.cpuUser->count()) / 1000000,
            ((double) buildResult.cpuSystem->count()) / 1000000);
    }

    /* Check the exit status. */
    if (!statusOk(status)) {

        /* Check *before* cleaning up. */
        bool diskFull = decideWhetherDiskFull();

        cleanupBuild(false);

        throw BuilderFailureError{
            !derivationType.isSandboxed() || diskFull ? BuildResult::Failure::TransientFailure
                                                      : BuildResult::Failure::PermanentFailure,
            status,
            diskFull ? "\nnote: build failure may have been caused by lack of free disk space" : "",
        };
    }

    SingleDrvOutputs builtOutputs;
    if (usingSubmitted) {
        builtOutputs = checkSubmittedOutputs();
    } else {
        /* Compute the FS closure of the outputs and register them as
           being valid. */
        builtOutputs = registerOutputs();
    }

    cleanupBuild(true);

    return builtOutputs;
}

bool UnixDerivationBuilderImpl::decideWhetherDiskFull()
{
    bool diskFull = false;

    /* Heuristically check whether the build failure may have
       been caused by a disk full condition.  We have no way
       of knowing whether the build actually got an ENOSPC.
       So instead, check if the disk is (nearly) full now.  If
       so, we don't mark this build as a permanent failure. */
#if HAVE_STATVFS
    {
        uint64_t required = 8ULL * 1024 * 1024; // FIXME: make configurable
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

void rethrowExceptionAsError()
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

void handleChildException(bool sendException)
{
    try {
        rethrowExceptionAsError();
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

static void checkNotWorldWritable(std::filesystem::path path)
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

std::optional<Descriptor> UnixDerivationBuilderImpl::startBuild()
{
    if (useBuildUsers(localSettings)) {
        if (!buildUser)
            buildUser = getBuildUser();

        if (!buildUser)
            return std::nullopt;
    }

    /* Make sure that no other processes are executing under the
       sandbox uids. This must be done before any chownToBuilder()
       calls. */
    prepareUser();

    auto buildDir = store.config->getBuildDir();

    createDirs(buildDir);

    if (buildUser)
        checkNotWorldWritable(buildDir);

    /* Create a temporary directory where the build will take
       place. */
    topTmpDir = createTempDir(buildDir, "nix", 0700);
    setBuildTmpDir();
    assert(!tmpDir.empty());

    /* The TOCTOU between the previous mkdir call and this open call is unavoidable due to
       POSIX semantics.*/
    tmpDirFd = openDirectory(tmpDir, FinalSymlink::DontFollow);
    if (!tmpDirFd)
        throw SysError("failed to open the build temporary directory descriptor %1%", PathFmt(tmpDir));

    chownToBuilder(tmpDirFd.get(), tmpDir);

    for (auto & [outputName, status] : initialOutputs) {
        /* Set scratch path we'll actually use during the build.

           If we're not doing a chroot build, but we have some valid
           output paths.  Since we can't just overwrite or delete
           them, we have to do hash rewriting: i.e. in the
           environment/arguments passed to the build, we replace the
           hashes of the valid outputs with unique dummy strings;
           after the build, we discard the redirected outputs
           corresponding to the valid outputs, and rewrite the
           contents of the new outputs to replace the dummy strings
           with the actual hashes. */
        auto scratchPath = !status.known ? makeFallbackPath(outputName)
                           : !needsHashRewrite()
                               /* Can always use original path in sandbox */
                               ? status.known->path
                               : !status.known->isPresent()
                                     /* If path doesn't yet exist can just use it */
                                     ? status.known->path
                                     : buildMode != bmRepair && !status.known->isValid()
                                           /* If we aren't repairing we'll delete a corrupted path, so we
                                              can use original path */
                                           ? status.known->path
                                           : /* If we are repairing or the path is totally valid, we'll need
                                                to use a temporary path */
                                           makeFallbackPath(status.known->path);
        scratchOutputs.insert_or_assign(outputName, scratchPath);

        /* Substitute output placeholders with the scratch output paths.
           We'll use during the build. */
        inputRewrites[hashPlaceholder(outputName)] = store.printStorePath(scratchPath);

        /* Additional tasks if we know the final path a priori. */
        if (!status.known)
            continue;
        auto fixedFinalPath = status.known->path;

        /* Additional tasks if the final and scratch are both known and
           differ. */
        if (fixedFinalPath == scratchPath)
            continue;

        /* Ensure scratch path is ours to use. */
        deletePath(store.printStorePath(scratchPath));

        /* Rewrite and unrewrite paths */
        {
            std::string h1{fixedFinalPath.hashPart()};
            std::string h2{scratchPath.hashPart()};
            inputRewrites[h1] = h2;
        }

        redirectedOutputs.insert_or_assign(std::move(fixedFinalPath), std::move(scratchPath));
    }

    /* Construct the environment passed to the builder. */
    initEnv();

    prepareSandbox();

    if (needsHashRewrite() && pathExists(homeDir))
        throw Error(
            "home directory %1% exists; please remove it to assure purity of builds without sandboxing",
            PathFmt(homeDir));

    /* Fire up a Nix daemon to process recursive Nix calls from the
       builder. */
    auto requiredFeatures = drvOptions.getRequiredSystemFeatures(drv);

    usingSubmitted = requiredFeatures.count(drvFeatureBuilderRpcV0);

    if (usingSubmitted && !type(drv).isCA()) {
        throw Error("The builder-rpc-v0 feature may only be used with content-addressing derivations");
    }

    if (usingSubmitted || requiredFeatures.count("recursive-nix"))
        startDaemon();

    /* Run the builder. */
    printMsg(lvlChatty, "executing builder '%1%'", drv.builder);
    printMsg(lvlChatty, "using builder args '%1%'", concatStringsSep(" ", drv.args));
    for (auto & i : drv.env)
        printMsg(lvlVomit, "setting builder env variable '%1%'='%2%'", i.first, i.second);

    /* Create the log file. */
    miscMethods->openLogFile();

    /* Create a pseudoterminal to get the output of the builder. */
    builderOut = posix_openpt(O_RDWR | O_NOCTTY);
    if (!builderOut)
        throw SysError("opening pseudoterminal master");

    std::string slaveName = getPtsName(builderOut.get());

    if (buildUser) {
        chmod(slaveName, 0600);

        chown(slaveName, buildUser->getUID(), 0);
    }
#ifdef __APPLE__
    else {
        if (grantpt(builderOut.get()))
            throw SysError("granting access to pseudoterminal slave");
    }
#endif

    if (unlockpt(builderOut.get()))
        throw SysError("unlocking pseudoterminal");

    buildResult.startTime = time(nullptr);

    /* Start a child process to build the derivation. */
    startChild();

    pid.setSeparatePG(true);

    processSandboxSetupMessages();

    return builderOut.get();
}

PathsInChroot UnixDerivationBuilderImpl::getPathsInSandbox()
{
    /* Allow a user-configurable set of directories from the
       host file system. */
    PathsInChroot pathsInChroot = defaultPathsInChroot;

    if (hasPrefix(store.storeDir, tmpDirInSandbox().native())) {
        throw Error("`sandbox-build-dir` must not contain the storeDir");
    }
    pathsInChroot[tmpDirInSandbox()] = {.source = tmpDir};

    auto allowedPaths = localSettings.allowedImpureHostPrefixes.get();

    /* This works like the above, except on a per-derivation level */
    auto impurePaths = drvOptions.impureHostDeps;

    for (auto & i : impurePaths) {
        bool found = false;
        /* Note: we're not resolving symlinks here to prevent
           giving a non-root user info about inaccessible
           files. */
        std::filesystem::path canonI = canonPath(i);
        /* If only we had a trie to do this more efficiently :) luckily, these are generally going to be pretty small */
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

        /* Allow files in drvOptions.impureHostDeps to be missing; e.g.
           macOS 11+ has no /usr/lib/libSystem*.dylib */
        pathsInChroot[i] = {i, true};
    }

    if (localSettings.preBuildHook != "") {
        printMsg(lvlChatty, "executing pre-build hook '%1%'", localSettings.preBuildHook);

        enum BuildHookState { stBegin, stExtraChrootDirs };

        auto state = stBegin;
        auto lines = runProgram(localSettings.preBuildHook.get(), false, getPreBuildHookArgs());
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

    return pathsInChroot;
}

void UnixDerivationBuilderImpl::prepareSandbox()
{
    if (drvOptions.useUidRange(drv))
        throw Error("feature 'uid-range' is not supported on this platform");
}

void UnixDerivationBuilderImpl::openSlave()
{
    std::string slaveName = getPtsName(builderOut.get());

    AutoCloseFD builderOut = open(slaveName.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (!builderOut)
        throw SysError("opening pseudoterminal slave");

    // Put the pt into raw mode to prevent \n -> \r\n translation.
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
std::optional<AwsCredentials> UnixDerivationBuilderImpl::preResolveAwsCredentials()
{
    if (drv.isBuiltin() && drv.builder == "builtin:fetchurl") {
        auto url = drv.env.find("url");
        if (url != drv.env.end()) {
            try {
                auto parsedUrl = parseURL(url->second);
                if (parsedUrl.scheme == "s3") {
                    debug("Pre-resolving AWS credentials for S3 URL in builtin:fetchurl");
                    auto s3Url = ParsedS3URL::parse(parsedUrl);

                    // Use the preResolveAwsCredentials from aws-creds
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

void UnixDerivationBuilderImpl::startChild()
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

void UnixDerivationBuilderImpl::processSandboxSetupMessages()
{
    std::vector<std::string> msgs;
    while (true) {
        std::string msg = [&]() {
            try {
                return readLine(builderOut.get());
            } catch (Error & e) {
                auto status = pid != unix::INVALID_PID ? pid.wait() : 0;
                e.addTrace(
                    {},
                    "while waiting for the build environment for '%s' to initialize (%s, previous messages: %s)",
                    store.printStorePath(drvPath),
                    status ? statusToString(status) : "no status",
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
            throw std::move(ex);
        }
        debug("sandbox setup: " + msg);
        msgs.push_back(std::move(msg));
    }
}

void UnixDerivationBuilderImpl::initEnv()
{
    env.clear();

    /* Most shells initialise PATH to some default (/bin:/usr/bin:...) when
       PATH is not set.  We don't want this, so we fill it in with some dummy
       value. */
    env["PATH"] = "/path-not-set";

    /* Set HOME to a non-existing path to prevent certain programs from using
       /etc/passwd (or NIS, or whatever) to locate the home directory (for
       example, wget looks for ~/.wgetrc).  I.e., these tools use /etc/passwd
       if HOME is not set, but they will just assume that the settings file
       they are looking for does not exist if HOME is set but points to some
       non-existing path. */
    env["HOME"] = homeDir;

    /* Tell the builder where the Nix store is.  Usually they
       shouldn't care, but this is useful for purity checking (e.g.,
       the compiler or linker might only want to accept paths to files
       in the store or in the build directory). */
    env["NIX_STORE"] = store.storeDir;

    /* The maximum number of cores to utilize for parallel building. */
    env["NIX_BUILD_CORES"] = fmt(
        "%d",
        settings.getLocalSettings().buildCores ? settings.getLocalSettings().buildCores : settings.getDefaultCores());

    /* Write the final environment. Note that this is intentionally
       *not* `drv.env`, because we've desugared things like like
       "passAFile", "expandReferencesGraph", structured attrs, etc. */
    for (const auto & [name, info] : desugaredEnv.variables) {
        env[name] = info.prependBuildDirectory ? (tmpDirInSandbox() / info.value).string() : info.value;
    }

    /* Add extra files, similar to `finalEnv` */
    for (const auto & [fileName, value] : desugaredEnv.extraFiles) {
        writeBuilderFile(fileName, rewriteStrings(value, inputRewrites));
    }

    /* For convenience, set an environment pointing to the top build
       directory. */
    env["NIX_BUILD_TOP"] = tmpDirInSandbox();

    /* Also set TMPDIR and variants to point to this directory. */
    env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = tmpDirInSandbox();

    /* Explicitly set PWD to prevent problems with chroot builds.  In
       particular, dietlibc cannot figure out the cwd because the
       inode of the current directory doesn't appear in .. (because
       getdents returns the inode of the mount point). */
    env["PWD"] = tmpDirInSandbox();

    /* Compatibility hack with Nix <= 0.7: if this is a fixed-output
       derivation, tell the builder, so that for instance `fetchurl'
       can skip checking the output.  On older Nixes, this environment
       variable won't be set, so `fetchurl' will do the check. */
    if (derivationType.isFixed())
        env["NIX_OUTPUT_CHECKED"] = "1";

    /* *Only* if this is a fixed-output derivation, propagate the
       values of the environment variables specified in the
       `impureEnvVars' attribute to the builder.  This allows for
       instance environment variables for proxy configuration such as
       `http_proxy' to be easily passed to downloaders like
       `fetchurl'.  Passing such environment variables from the caller
       to the builder is generally impure, but the output of
       fixed-output derivations is by definition pure (since we
       already know the cryptographic hash of the output). */
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

    /* Currently structured log messages piggyback on stderr, but we
       may change that in the future. So tell the builder which file
       descriptor to use for that. */
    env["NIX_LOG_FD"] = "2";

    /* Trigger colored output in various tools. */
    env["TERM"] = "xterm-256color";
}

void UnixDerivationBuilderImpl::startDaemon()
{
    if (usingSubmitted) {
        experimentalFeatureSettings.require(Xp::DynamicDerivations);
    } else {
        experimentalFeatureSettings.require(Xp::RecursiveNix);
    }

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

    state_.lock()->addedPaths.clear();

    auto socketName = ".nix-socket";
    std::filesystem::path socketPath = tmpDir / socketName;
    env["NIX_REMOTE"] = "unix://" + (tmpDirInSandbox() / socketName).native();

    daemonSocket = createUnixDomainSocket(socketPath, 0600);

    chownToBuilder(socketPath);

    daemon::RecursiveFlag recursiveFlag;
    if (usingSubmitted) {
        recursiveFlag = daemon::RecursiveFlag::RecursiveSubmitted;
    } else {
        recursiveFlag = daemon::RecursiveFlag::Recursive;
    }

    daemonThread = std::thread([this, store, recursiveFlag]() {
        while (true) {

            /* Accept a connection. */
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

            auto doneFlag = make_ref<std::atomic_flag>();

            auto workerThread = std::thread([this, doneFlag, store, remote{std::move(remote)}, recursiveFlag]() {
                try {
                    miscMethods->processDaemonConnection(
                        store, FdSource(remote.get()), FdSink(remote.get()), *this, recursiveFlag);
                    debug("terminated daemon connection");
                } catch (const Interrupted &) {
                    debug("interrupted daemon connection");
                } catch (...) {
                    /* Swallow all exceptions to avoid crashing the the process (exceptions that escape from the thread
                     * trigger std::terminate()). */
                    ignoreExceptionExceptInterrupt();
                }

                doneFlag->test_and_set(std::memory_order_relaxed);
            });

            daemonWorkerThreads.push_back(
                DaemonWorkerState{
                    .thread = std::move(workerThread),
                    .done = std::move(doneFlag),
                });

            /* Prune threads eagerly to free up resources. Ideally we'd also limit the number of concurrent workers. */
            for (auto it = daemonWorkerThreads.begin(), end = daemonWorkerThreads.end(); it != end;) {
                auto & state = *it;
                auto & thread = state.thread;
                if (state.done->test(std::memory_order_relaxed) && thread.joinable()) {
                    thread.join();
                    it = daemonWorkerThreads.erase(it);
                } else {
                    ++it;
                }
            }
        }

        debug("daemon shutting down");
    });
}

void UnixDerivationBuilderImpl::stopDaemon()
{
    if (daemonSocket && shutdown(daemonSocket.get(), SHUT_RDWR) == -1) {
        // According to the POSIX standard, the 'shutdown' function should
        // return an ENOTCONN error when attempting to shut down a socket that
        // hasn't been connected yet. This situation occurs when the 'accept'
        // function is called on a socket without any accepted connections,
        // leaving the socket unconnected. While Linux doesn't seem to produce
        // an error for sockets that have only been accepted, more
        // POSIX-compliant operating systems like OpenBSD, macOS, and others do
        // return the ENOTCONN error. Therefore, we handle this error here to
        // avoid raising an exception for compliant behaviour.
        if (errno == ENOTCONN) {
            daemonSocket.close();
        } else {
            throw SysError("shutting down daemon socket");
        }
    }

    if (daemonThread.joinable())
        daemonThread.join();

    for (auto & [thread, doneFlag] : daemonWorkerThreads)
        thread.join();
    daemonWorkerThreads.clear();

    // release the socket.
    daemonSocket.close();
}

void UnixDerivationBuilderImpl::addDependencyImpl(const StorePath & path) {}

void UnixDerivationBuilderImpl::chownToBuilder(const std::filesystem::path & path)
{
    if (!buildUser)
        return;
    chown(path, buildUser->getUID(), buildUser->getGID());
}

void UnixDerivationBuilderImpl::chownToBuilder(int fd, const std::filesystem::path & path)
{
    if (!buildUser)
        return;
    if (fchown(fd, buildUser->getUID(), buildUser->getGID()) == -1)
        throw SysError("cannot change ownership of file %1%", PathFmt(path));
}

void UnixDerivationBuilderImpl::writeBuilderFile(const std::string & name, std::string_view contents)
{
    /* Path must be the same after normalisation. This is an additional sanity check in addition to
       existing parsing checks for non-structured attrs exportReferencesGraph. In practice we only expect
       a single path component without any `..`, `.` components. */
    auto relPath = CanonPath::fromFilename(name);
    AutoCloseFD fd = openFileEnsureBeneathNoSymlinks(
        tmpDirFd.get(), relPath, O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC | O_EXCL, 0666);
    auto path = tmpDir / relPath.rel(); /* This is used only for error messages. */
    if (!fd)
        throw SysError("creating file %s", PathFmt(path));
    writeFile(fd.get(), contents);
    chownToBuilder(fd.get(), path);
}

void UnixDerivationBuilderImpl::runChild(RunChildArgs args)
{
    /* Warning: in the child we should absolutely not make any SQLite
       calls! */

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
#if NIX_WITH_AWS_AUTH
            .awsCredentials = args.awsCredentials,
#endif
        };

        if (drv.isBuiltin() && drv.builder == "builtin:fetchurl") {
            try {
                ctx.netrcData = readFile(fileTransferSettings.netrcFile.get());
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

        /* Make sure the builder inherits a predictable umask. It must not be group-writable, since registerOutputs
         * rejects those as defense-in-depth. */
        umask(0022);

        // FIXME: set other limits to deterministic values?

        setUser();

        /* Indicate that we managed to set up the build environment. */
        writeFull(STDERR_FILENO, std::string("\2\n"));

        sendException = false;

        /* If this is a builtin builder, call it now. This should not return. */
        if (drv.isBuiltin()) {
            try {
                logger = makeJSONLogger(getStandardError()).release();

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

        Strings args;
        args.push_back(std::string(baseNameOf(drv.builder)));

        for (auto & i : drv.args)
            args.push_back(rewriteStrings(i, inputRewrites));

        Strings envStrs;
        for (auto & i : env)
            envStrs.push_back(rewriteStrings(i.first + "=" + i.second, inputRewrites));

        execBuilder(args, envStrs);

        throw SysError("executing '%1%'", drv.builder);

    } catch (...) {
        handleChildException(sendException);
        _exit(1);
    }
}

void UnixDerivationBuilderImpl::setUser()
{
    /* If we are running in `build-users' mode, then switch to the
       user we allocated above.  Make sure that we drop all root
       privileges.  Note that above we have closed all file
       descriptors except std*, so that's safe.  Also note that
       setuid() when run as root sets the real, effective and
       saved UIDs. */
    if (buildUser) {
        preserveDeathSignal([this]() {
            /* Preserve supplementary groups of the build user, to allow
               admins to specify groups such as "kvm".  */
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

void UnixDerivationBuilderImpl::execBuilder(const Strings & args, const Strings & envStrs)
{
    execve(requireCString(drv.builder), stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());
}

SingleDrvOutputs UnixDerivationBuilderImpl::checkSubmittedOutputs()
{
    // Submitted outputs from the recursive nix daemon
    // It's fine to lock here since all other threads with the reference have been shut down.
    auto submittedOutputs(this->submittedOutputs.lock());

    SingleDrvOutputs builtOutputs;

    std::map<std::string, ValidPathInfo> infos;

    for (auto & [outputName, outputPath] : *submittedOutputs) {
        infos.emplace(outputName, *store.queryPathInfo(outputPath));
    }

    // checkOutputs only performs checks that make sense for both submitting and non-submitting derivations,
    // more verification steps needed afterward
    checkOutputs(store, drvPath, drv, drvOptions.outputChecks, infos);

    for (auto & [outputName, output] : drv.outputs) {
        // For some reason cannot be moved to checkOutputs, needs debugging
        if (!submittedOutputs->contains(outputName)) {
            throw BuildError(
                BuildResult::Failure::OutputRejected,
                "builder for '%s' failed to submit output path for '%s'",
                store.printStorePath(drvPath),
                outputName);
        }

        // We should have already checked that the derivation is content-addressing in startBuild
        // and that the outputs of a content-addressing derivation is content-addressed in checkOutputs.
        // Add an assert here just in case, but it should never trigger.
        assert(
            std::get_if<DerivationOutput::CAFloating>(&output.raw)
            || std::get_if<DerivationOutput::CAFixed>(&output.raw));

        // No need to sign CA outputs, only the realisation matters
        auto realisation = Realisation{
            {
                .outPath = *get(*submittedOutputs, outputName),
            },
            DrvOutput{
                .drvPath = drvPath,
                .outputName = outputName,
            },
        };

        store.signRealisation(realisation);
        store.registerDrvOutput(realisation, NoCheckSigs);
        builtOutputs.emplace(outputName, realisation);

        // TODO: handle --check
    }

    return builtOutputs;
}

void UnixDerivationBuilderImpl::cleanupBuild(bool force)
{
    if (force) {
        /* Delete unused redirected outputs (when doing hash rewriting). */
        for (auto & i : redirectedOutputs)
            deletePath(store.toRealPath(i.second));
    }

    if (topTmpDir != "") {
        /* As an extra precaution, even in the event of `deletePath` failing to
         * clean up, the `tmpDir` will be chowned as if we were to move
         * it inside the Nix store.
         *
         * This hardens against an attack which smuggles a file descriptor
         * to make use of the temporary directory.
         */
        chmod(topTmpDir, 0000);

        /* Don't keep temporary directories for builtins because they
           might have privileged stuff (like a copy of netrc). */
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

StorePath UnixDerivationBuilderImpl::makeFallbackPath(OutputNameView outputName)
{
    // This is a bogus path type, constructed this way to ensure that it doesn't collide with any other store path
    // See doc/manual/source/protocols/store-path.md for details
    // TODO: We may want to separate the responsibilities of constructing the path fingerprint and of actually doing the
    // hashing
    auto pathType = "rewrite:" + std::string(drvPath.to_string()) + ":name:" + std::string(outputName);
    return store.makeStorePath(
        pathType,
        // pass an all-zeroes hash
        Hash(HashAlgorithm::SHA256),
        outputPathName(drv.name, outputName));
}

StorePath UnixDerivationBuilderImpl::makeFallbackPath(const StorePath & path)
{
    // This is a bogus path type, constructed this way to ensure that it doesn't collide with any other store path
    // See doc/manual/source/protocols/store-path.md for details
    auto pathType = "rewrite:" + std::string(drvPath.to_string()) + ":" + std::string(path.to_string());
    return store.makeStorePath(
        pathType,
        // pass an all-zeroes hash
        Hash(HashAlgorithm::SHA256),
        path.name());
}

} // namespace nix

namespace nix {

void DerivationBuilderDeleter::operator()(DerivationBuilder * builder) noexcept
{
    if (!builder) /* Idempotent and handles nullptr as any deleter must. */
        return;

    if (auto builderImpl = dynamic_cast<UnixDerivationBuilderImpl *>(builder))
        /* Note that this might call into virtual functions, which we can't do in a destructor of
           the UnixDerivationBuilderImpl itself. */
        builderImpl->cleanupOnDestruction();

    delete builder;
}

std::unique_ptr<DerivationBuilder, DerivationBuilderDeleter> makeDerivationBuilder(
    LocalStore & store, std::shared_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
{
    bool useSandbox = false;
    const LocalSettings & localSettings = store.config->getLocalSettings();

    /* Are we doing a sandboxed build? */
    {
        if (localSettings.sandboxMode == smEnabled) {
            if (params.drvOptions.noChroot)
                throw Error(
                    "derivation '%s' has '__noChroot' set, "
                    "but that's not allowed when 'sandbox' is 'true'",
                    store.printStorePath(params.drvPath));
#ifdef __APPLE__
            if (params.drvOptions.additionalSandboxProfile != "")
                throw Error(
                    "derivation '%s' specifies a sandbox profile, "
                    "but this is only allowed when 'sandbox' is 'relaxed'",
                    store.printStorePath(params.drvPath));
#endif
            useSandbox = true;
        } else if (localSettings.sandboxMode == smDisabled)
            useSandbox = false;
        else if (localSettings.sandboxMode == smRelaxed)
            // FIXME: cache derivationType
            useSandbox = type(params.drv).isSandboxed() && !params.drvOptions.noChroot;
    }

    const bool isRelocatedStore = store.storeDir != store.config->realStoreDir.get();

    if (isRelocatedStore) {
#if defined(__linux__) || defined(__FreeBSD__)
        useSandbox = true;
#else
        throw Error("building using a diverted store is not supported on this platform");
#endif
    }

#ifdef __linux__
    if (useSandbox && !mountAndPidNamespacesSupported()) {
        if (!localSettings.sandboxFallback)
            throw Error(
                "this system does not support the kernel namespaces that are required for sandboxing; use '--no-sandbox' to disable sandboxing");

        if (isRelocatedStore)
            throw Error(
                "this system does not support the kernel namespaces that are required for sandboxing, which is required for building in a diverted store");

        static std::atomic<bool> warned = false;
        warnOnce(
            warned,
            "auto-disabling sandboxing because the prerequisite namespaces are not available and '%1%' is enabled; use '--no-sandbox' or specify 'sandbox = false' setting to silence this warning",
            localSettings.sandboxFallback.name);

        useSandbox = false;
    }
#endif

    if (!useSandbox && params.drvOptions.useUidRange(params.drv))
        throw Error("feature 'uid-range' is only supported in sandboxed builds");

#ifdef __APPLE__
    return DerivationBuilderUnique(new DarwinDerivationBuilder(store, miscMethods, std::move(params), useSandbox));
#elif defined(__linux__)
    if (useSandbox)
        return DerivationBuilderUnique(new ChrootLinuxDerivationBuilder(store, miscMethods, std::move(params)));

    return DerivationBuilderUnique(new LinuxDerivationBuilder(store, miscMethods, std::move(params)));
#elif defined(__FreeBSD__)
    if (useSandbox)
        return DerivationBuilderUnique(new ChrootFreeBSDDerivationBuilder(store, miscMethods, std::move(params)));

    return DerivationBuilderUnique(new FreeBSDDerivationBuilder(store, miscMethods, std::move(params)));
#else
    if (useSandbox)
        throw Error("sandboxing builds is not supported on this platform");

    return DerivationBuilderUnique(new UnixDerivationBuilderImpl(store, miscMethods, std::move(params)));
#endif
}

} // namespace nix
