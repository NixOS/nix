#include "local-derivation-goal.hh"
#include "hook-instance.hh"
#include "worker.hh"
#include "builtins.hh"
#include "builtins/buildenv.hh"
#include "references.hh"
#include "finally.hh"
#include "util.hh"
#include "archive.hh"
#include "json.hh"
#include "compression.hh"
#include "daemon.hh"
#include "worker-protocol.hh"
#include "topo-sort.hh"
#include "callback.hh"

#include <regex>
#include <queue>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <sys/resource.h>

#if HAVE_STATVFS
#include <sys/statvfs.h>
#endif

/* Includes required for chroot support. */
#if __linux__
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <sys/personality.h>
#include <sys/mman.h>
#include <sched.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#if HAVE_SECCOMP
#include <seccomp.h>
#endif
#define pivot_root(new_root, put_old) (syscall(SYS_pivot_root, new_root, put_old))
#endif

#if __APPLE__
#include <spawn.h>
#include <sys/sysctl.h>
#endif

#include <pwd.h>
#include <grp.h>

#include <nlohmann/json.hpp>

namespace nix {

void handleDiffHook(
    uid_t uid, uid_t gid,
    const Path & tryA, const Path & tryB,
    const Path & drvPath, const Path & tmpDir)
{
    auto diffHook = settings.diffHook;
    if (diffHook != "" && settings.runDiffHook) {
        try {
            RunOptions diffHookOptions(diffHook,{tryA, tryB, drvPath, tmpDir});
            diffHookOptions.searchPath = true;
            diffHookOptions.uid = uid;
            diffHookOptions.gid = gid;
            diffHookOptions.chdir = "/";
            auto diffRes = runProgram(diffHookOptions);
            if (!statusOk(diffRes.first))
                throw ExecError(diffRes.first,
                    "diff-hook program '%1%' %2%",
                    diffHook,
                    statusToString(diffRes.first));

            if (diffRes.second != "")
                printError(chomp(diffRes.second));
        } catch (Error & error) {
            ErrorInfo ei = error.info();
            // FIXME: wrap errors.
            ei.msg = hintfmt("diff hook execution failed: %s", ei.msg.str());
            logError(ei);
        }
    }
}

const Path LocalDerivationGoal::homeDir = "/homeless-shelter";


LocalDerivationGoal::~LocalDerivationGoal()
{
    /* Careful: we should never ever throw an exception from a
       destructor. */
    try { deleteTmpDir(false); } catch (...) { ignoreException(); }
    try { killChild(); } catch (...) { ignoreException(); }
    try { stopDaemon(); } catch (...) { ignoreException(); }
}


inline bool LocalDerivationGoal::needsHashRewrite()
{
#if __linux__
    return !useChroot;
#else
    /* Darwin requires hash rewriting even when sandboxing is enabled. */
    return true;
#endif
}


LocalStore & LocalDerivationGoal::getLocalStore()
{
    auto p = dynamic_cast<LocalStore *>(&worker.store);
    assert(p);
    return *p;
}


void LocalDerivationGoal::killChild()
{
    if (pid != -1) {
        worker.childTerminated(this);

        if (buildUser) {
            /* If we're using a build user, then there is a tricky
               race condition: if we kill the build user before the
               child has done its setuid() to the build user uid, then
               it won't be killed, and we'll potentially lock up in
               pid.wait().  So also send a conventional kill to the
               child. */
            ::kill(-pid, SIGKILL); /* ignore the result */
            buildUser->kill();
            pid.wait();
        } else
            pid.kill();

        assert(pid == -1);
    }

    DerivationGoal::killChild();
}


void LocalDerivationGoal::tryLocalBuild() {
    unsigned int curBuilds = worker.getNrLocalBuilds();
    if (curBuilds >= settings.maxBuildJobs) {
        worker.waitForBuildSlot(shared_from_this());
        outputLocks.unlock();
        return;
    }

    /* If `build-users-group' is not empty, then we have to build as
       one of the members of that group. */
    if (settings.buildUsersGroup != "" && getuid() == 0) {
#if defined(__linux__) || defined(__APPLE__)
        if (!buildUser) buildUser = std::make_unique<UserLock>();

        if (buildUser->findFreeUser()) {
            /* Make sure that no other processes are executing under this
               uid. */
            buildUser->kill();
        } else {
            if (!actLock)
                actLock = std::make_unique<Activity>(*logger, lvlWarn, actBuildWaiting,
                    fmt("waiting for UID to build '%s'", yellowtxt(worker.store.printStorePath(drvPath))));
            worker.waitForAWhile(shared_from_this());
            return;
        }
#else
        /* Don't know how to block the creation of setuid/setgid
           binaries on this platform. */
        throw Error("build users are not supported on this platform for security reasons");
#endif
    }

    actLock.reset();

    try {

        /* Okay, we have to build. */
        startBuilder();

    } catch (BuildError & e) {
        outputLocks.unlock();
        buildUser.reset();
        worker.permanentFailure = true;
        done(BuildResult::InputRejected, e);
        return;
    }

    /* This state will be reached when we get EOF on the child's
       log pipe. */
    state = &DerivationGoal::buildDone;

    started();
}

static void chmod_(const Path & path, mode_t mode)
{
    if (chmod(path.c_str(), mode) == -1)
        throw SysError("setting permissions on '%s'", path);
}


/* Move/rename path 'src' to 'dst'. Temporarily make 'src' writable if
   it's a directory and we're not root (to be able to update the
   directory's parent link ".."). */
static void movePath(const Path & src, const Path & dst)
{
    auto st = lstat(src);

    bool changePerm = (geteuid() && S_ISDIR(st.st_mode) && !(st.st_mode & S_IWUSR));

    if (changePerm)
        chmod_(src, st.st_mode | S_IWUSR);

    if (rename(src.c_str(), dst.c_str()))
        throw SysError("renaming '%1%' to '%2%'", src, dst);

    if (changePerm)
        chmod_(dst, st.st_mode);
}


extern void replaceValidPath(const Path & storePath, const Path & tmpPath);


int LocalDerivationGoal::getChildStatus()
{
    return hook ? DerivationGoal::getChildStatus() : pid.kill();
}

void LocalDerivationGoal::closeReadPipes()
{
    if (hook) {
        DerivationGoal::closeReadPipes();
    } else
        builderOut.readSide = -1;
}


void LocalDerivationGoal::cleanupHookFinally()
{
    /* Release the build user at the end of this function. We don't do
       it right away because we don't want another build grabbing this
       uid and then messing around with our output. */
    buildUser.reset();
}


void LocalDerivationGoal::cleanupPreChildKill()
{
    sandboxMountNamespace = -1;
}


void LocalDerivationGoal::cleanupPostChildKill()
{
    /* When running under a build user, make sure that all processes
       running under that uid are gone.  This is to prevent a
       malicious user from leaving behind a process that keeps files
       open and modifies them after they have been chown'ed to
       root. */
    if (buildUser) buildUser->kill();

    /* Terminate the recursive Nix daemon. */
    stopDaemon();
}


bool LocalDerivationGoal::cleanupDecideWhetherDiskFull()
{
    bool diskFull = false;

    /* Heuristically check whether the build failure may have
       been caused by a disk full condition.  We have no way
       of knowing whether the build actually got an ENOSPC.
       So instead, check if the disk is (nearly) full now.  If
       so, we don't mark this build as a permanent failure. */
#if HAVE_STATVFS
    {
        auto & localStore = getLocalStore();
        uint64_t required = 8ULL * 1024 * 1024; // FIXME: make configurable
        struct statvfs st;
        if (statvfs(localStore.realStoreDir.c_str(), &st) == 0 &&
            (uint64_t) st.f_bavail * st.f_bsize < required)
            diskFull = true;
        if (statvfs(tmpDir.c_str(), &st) == 0 &&
            (uint64_t) st.f_bavail * st.f_bsize < required)
            diskFull = true;
    }
#endif

    deleteTmpDir(false);

    /* Move paths out of the chroot for easier debugging of
       build failures. */
    if (useChroot && buildMode == bmNormal)
        for (auto & [_, status] : initialOutputs) {
            if (!status.known) continue;
            if (buildMode != bmCheck && status.known->isValid()) continue;
            auto p = worker.store.printStorePath(status.known->path);
            if (pathExists(chrootRootDir + p))
                rename((chrootRootDir + p).c_str(), p.c_str());
        }

    return diskFull;
}


void LocalDerivationGoal::cleanupPostOutputsRegisteredModeCheck()
{
    deleteTmpDir(true);
}


void LocalDerivationGoal::cleanupPostOutputsRegisteredModeNonCheck()
{
    /* Delete unused redirected outputs (when doing hash rewriting). */
    for (auto & i : redirectedOutputs)
        deletePath(worker.store.Store::toRealPath(i.second));

    /* Delete the chroot (if we were using one). */
    autoDelChroot.reset(); /* this runs the destructor */

    cleanupPostOutputsRegisteredModeCheck();
}


int childEntry(void * arg)
{
    ((LocalDerivationGoal *) arg)->runChild();
    return 1;
}


static std::once_flag dns_resolve_flag;

static void preloadNSS() {
    /* builtin:fetchurl can trigger a DNS lookup, which with glibc can trigger a dynamic library load of
       one of the glibc NSS libraries in a sandboxed child, which will fail unless the library's already
       been loaded in the parent. So we force a lookup of an invalid domain to force the NSS machinery to
       load its lookup libraries in the parent before any child gets a chance to. */
    std::call_once(dns_resolve_flag, []() {
        struct addrinfo *res = NULL;

        if (getaddrinfo("this.pre-initializes.the.dns.resolvers.invalid.", "http", NULL, &res) != 0) {
            if (res) freeaddrinfo(res);
        }
    });
}


static void linkOrCopy(const Path & from, const Path & to)
{
    if (link(from.c_str(), to.c_str()) == -1) {
        /* Hard-linking fails if we exceed the maximum link count on a
           file (e.g. 32000 of ext3), which is quite possible after a
           'nix-store --optimise'. FIXME: actually, why don't we just
           bind-mount in this case?

           It can also fail with EPERM in BeegFS v7 and earlier versions
           which don't allow hard-links to other directories */
        if (errno != EMLINK && errno != EPERM)
            throw SysError("linking '%s' to '%s'", to, from);
        copyPath(from, to);
    }
}


void LocalDerivationGoal::startBuilder()
{
    /* Right platform? */
    if (!parsedDrv->canBuildLocally(worker.store))
        throw Error("a '%s' with features {%s} is required to build '%s', but I am a '%s' with features {%s}",
            drv->platform,
            concatStringsSep(", ", parsedDrv->getRequiredSystemFeatures()),
            worker.store.printStorePath(drvPath),
            settings.thisSystem,
            concatStringsSep<StringSet>(", ", worker.store.systemFeatures));

    if (drv->isBuiltin())
        preloadNSS();

#if __APPLE__
    additionalSandboxProfile = parsedDrv->getStringAttr("__sandboxProfile").value_or("");
#endif

    /* Are we doing a chroot build? */
    {
        auto noChroot = parsedDrv->getBoolAttr("__noChroot");
        if (settings.sandboxMode == smEnabled) {
            if (noChroot)
                throw Error("derivation '%s' has '__noChroot' set, "
                    "but that's not allowed when 'sandbox' is 'true'", worker.store.printStorePath(drvPath));
#if __APPLE__
            if (additionalSandboxProfile != "")
                throw Error("derivation '%s' specifies a sandbox profile, "
                    "but this is only allowed when 'sandbox' is 'relaxed'", worker.store.printStorePath(drvPath));
#endif
            useChroot = true;
        }
        else if (settings.sandboxMode == smDisabled)
            useChroot = false;
        else if (settings.sandboxMode == smRelaxed)
            useChroot = !(derivationIsImpure(derivationType)) && !noChroot;
    }

    auto & localStore = getLocalStore();
    if (localStore.storeDir != localStore.realStoreDir) {
        #if __linux__
            useChroot = true;
        #else
            throw Error("building using a diverted store is not supported on this platform");
        #endif
    }

    /* Create a temporary directory where the build will take
       place. */
    tmpDir = createTempDir("", "nix-build-" + std::string(drvPath.name()), false, false, 0700);

    chownToBuilder(tmpDir);

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
        auto scratchPath =
            !status.known
                ? makeFallbackPath(outputName)
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
            :   /* If we are repairing or the path is totally valid, we'll need
                   to use a temporary path */
                makeFallbackPath(status.known->path);
        scratchOutputs.insert_or_assign(outputName, scratchPath);

        /* Substitute output placeholders with the scratch output paths.
           We'll use during the build. */
        inputRewrites[hashPlaceholder(outputName)] = worker.store.printStorePath(scratchPath);

        /* Additional tasks if we know the final path a priori. */
        if (!status.known) continue;
        auto fixedFinalPath = status.known->path;

        /* Additional tasks if the final and scratch are both known and
           differ. */
        if (fixedFinalPath == scratchPath) continue;

        /* Ensure scratch path is ours to use. */
        deletePath(worker.store.printStorePath(scratchPath));

        /* Rewrite and unrewrite paths */
        {
            std::string h1 { fixedFinalPath.hashPart() };
            std::string h2 { scratchPath.hashPart() };
            inputRewrites[h1] = h2;
        }

        redirectedOutputs.insert_or_assign(std::move(fixedFinalPath), std::move(scratchPath));
    }

    /* Construct the environment passed to the builder. */
    initEnv();

    writeStructuredAttrs();

    /* Handle exportReferencesGraph(), if set. */
    if (!parsedDrv->getStructuredAttrs()) {
        /* The `exportReferencesGraph' feature allows the references graph
           to be passed to a builder.  This attribute should be a list of
           pairs [name1 path1 name2 path2 ...].  The references graph of
           each `pathN' will be stored in a text file `nameN' in the
           temporary build directory.  The text files have the format used
           by `nix-store --register-validity'.  However, the deriver
           fields are left empty. */
        string s = get(drv->env, "exportReferencesGraph").value_or("");
        Strings ss = tokenizeString<Strings>(s);
        if (ss.size() % 2 != 0)
            throw BuildError("odd number of tokens in 'exportReferencesGraph': '%1%'", s);
        for (Strings::iterator i = ss.begin(); i != ss.end(); ) {
            string fileName = *i++;
            static std::regex regex("[A-Za-z_][A-Za-z0-9_.-]*");
            if (!std::regex_match(fileName, regex))
                throw Error("invalid file name '%s' in 'exportReferencesGraph'", fileName);

            auto storePathS = *i++;
            if (!worker.store.isInStore(storePathS))
                throw BuildError("'exportReferencesGraph' contains a non-store path '%1%'", storePathS);
            auto storePath = worker.store.toStorePath(storePathS).first;

            /* Write closure info to <fileName>. */
            writeFile(tmpDir + "/" + fileName,
                worker.store.makeValidityRegistration(
                    exportReferences({storePath}), false, false));
        }
    }

    if (useChroot) {

        /* Allow a user-configurable set of directories from the
           host file system. */
        dirsInChroot.clear();

        for (auto i : settings.sandboxPaths.get()) {
            if (i.empty()) continue;
            bool optional = false;
            if (i[i.size() - 1] == '?') {
                optional = true;
                i.pop_back();
            }
            size_t p = i.find('=');
            if (p == string::npos)
                dirsInChroot[i] = {i, optional};
            else
                dirsInChroot[string(i, 0, p)] = {string(i, p + 1), optional};
        }
        dirsInChroot[tmpDirInSandbox] = tmpDir;

        /* Add the closure of store paths to the chroot. */
        StorePathSet closure;
        for (auto & i : dirsInChroot)
            try {
                if (worker.store.isInStore(i.second.source))
                    worker.store.computeFSClosure(worker.store.toStorePath(i.second.source).first, closure);
            } catch (InvalidPath & e) {
            } catch (Error & e) {
                e.addTrace({}, "while processing 'sandbox-paths'");
                throw;
            }
        for (auto & i : closure) {
            auto p = worker.store.printStorePath(i);
            dirsInChroot.insert_or_assign(p, p);
        }

        PathSet allowedPaths = settings.allowedImpureHostPrefixes;

        /* This works like the above, except on a per-derivation level */
        auto impurePaths = parsedDrv->getStringsAttr("__impureHostDeps").value_or(Strings());

        for (auto & i : impurePaths) {
            bool found = false;
            /* Note: we're not resolving symlinks here to prevent
               giving a non-root user info about inaccessible
               files. */
            Path canonI = canonPath(i);
            /* If only we had a trie to do this more efficiently :) luckily, these are generally going to be pretty small */
            for (auto & a : allowedPaths) {
                Path canonA = canonPath(a);
                if (canonI == canonA || isInDir(canonI, canonA)) {
                    found = true;
                    break;
                }
            }
            if (!found)
                throw Error("derivation '%s' requested impure path '%s', but it was not in allowed-impure-host-deps",
                    worker.store.printStorePath(drvPath), i);

            dirsInChroot[i] = i;
        }

#if __linux__
        /* Create a temporary directory in which we set up the chroot
           environment using bind-mounts.  We put it in the Nix store
           to ensure that we can create hard-links to non-directory
           inputs in the fake Nix store in the chroot (see below). */
        chrootRootDir = worker.store.Store::toRealPath(drvPath) + ".chroot";
        deletePath(chrootRootDir);

        /* Clean up the chroot directory automatically. */
        autoDelChroot = std::make_shared<AutoDelete>(chrootRootDir);

        printMsg(lvlChatty, format("setting up chroot environment in '%1%'") % chrootRootDir);

        if (mkdir(chrootRootDir.c_str(), 0750) == -1)
            throw SysError("cannot create '%1%'", chrootRootDir);

        if (buildUser && chown(chrootRootDir.c_str(), 0, buildUser->getGID()) == -1)
            throw SysError("cannot change ownership of '%1%'", chrootRootDir);

        /* Create a writable /tmp in the chroot.  Many builders need
           this.  (Of course they should really respect $TMPDIR
           instead.) */
        Path chrootTmpDir = chrootRootDir + "/tmp";
        createDirs(chrootTmpDir);
        chmod_(chrootTmpDir, 01777);

        /* Create a /etc/passwd with entries for the build user and the
           nobody account.  The latter is kind of a hack to support
           Samba-in-QEMU. */
        createDirs(chrootRootDir + "/etc");

        /* Declare the build user's group so that programs get a consistent
           view of the system (e.g., "id -gn"). */
        writeFile(chrootRootDir + "/etc/group",
            fmt("root:x:0:\n"
                "nixbld:!:%1%:\n"
                "nogroup:x:65534:\n", sandboxGid()));

        /* Create /etc/hosts with localhost entry. */
        if (!(derivationIsImpure(derivationType)))
            writeFile(chrootRootDir + "/etc/hosts", "127.0.0.1 localhost\n::1 localhost\n");

        /* Make the closure of the inputs available in the chroot,
           rather than the whole Nix store.  This prevents any access
           to undeclared dependencies.  Directories are bind-mounted,
           while other inputs are hard-linked (since only directories
           can be bind-mounted).  !!! As an extra security
           precaution, make the fake Nix store only writable by the
           build user. */
        Path chrootStoreDir = chrootRootDir + worker.store.storeDir;
        createDirs(chrootStoreDir);
        chmod_(chrootStoreDir, 01775);

        if (buildUser && chown(chrootStoreDir.c_str(), 0, buildUser->getGID()) == -1)
            throw SysError("cannot change ownership of '%1%'", chrootStoreDir);

        for (auto & i : inputPaths) {
            auto p = worker.store.printStorePath(i);
            Path r = worker.store.toRealPath(p);
            if (S_ISDIR(lstat(r).st_mode))
                dirsInChroot.insert_or_assign(p, r);
            else
                linkOrCopy(r, chrootRootDir + p);
        }

        /* If we're repairing, checking or rebuilding part of a
           multiple-outputs derivation, it's possible that we're
           rebuilding a path that is in settings.dirsInChroot
           (typically the dependencies of /bin/sh).  Throw them
           out. */
        for (auto & i : drv->outputsAndOptPaths(worker.store)) {
            /* If the name isn't known a priori (i.e. floating
               content-addressed derivation), the temporary location we use
               should be fresh.  Freshness means it is impossible that the path
               is already in the sandbox, so we don't need to worry about
               removing it.  */
            if (i.second.second)
                dirsInChroot.erase(worker.store.printStorePath(*i.second.second));
        }

#elif __APPLE__
        /* We don't really have any parent prep work to do (yet?)
           All work happens in the child, instead. */
#else
        throw Error("sandboxing builds is not supported on this platform");
#endif
    }

    if (needsHashRewrite() && pathExists(homeDir))
        throw Error("home directory '%1%' exists; please remove it to assure purity of builds without sandboxing", homeDir);

    if (useChroot && settings.preBuildHook != "" && dynamic_cast<Derivation *>(drv.get())) {
        printMsg(lvlChatty, format("executing pre-build hook '%1%'")
            % settings.preBuildHook);
        auto args = useChroot ? Strings({worker.store.printStorePath(drvPath), chrootRootDir}) :
            Strings({ worker.store.printStorePath(drvPath) });
        enum BuildHookState {
            stBegin,
            stExtraChrootDirs
        };
        auto state = stBegin;
        auto lines = runProgram(settings.preBuildHook, false, args);
        auto lastPos = std::string::size_type{0};
        for (auto nlPos = lines.find('\n'); nlPos != string::npos;
                nlPos = lines.find('\n', lastPos)) {
            auto line = std::string{lines, lastPos, nlPos - lastPos};
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
                    if (p == string::npos)
                        dirsInChroot[line] = line;
                    else
                        dirsInChroot[string(line, 0, p)] = string(line, p + 1);
                }
            }
        }
    }

    /* Fire up a Nix daemon to process recursive Nix calls from the
       builder. */
    if (parsedDrv->getRequiredSystemFeatures().count("recursive-nix"))
        startDaemon();

    /* Run the builder. */
    printMsg(lvlChatty, "executing builder '%1%'", drv->builder);

    /* Create the log file. */
    Path logFile = openLogFile();

    /* Create a pipe to get the output of the builder. */
    //builderOut.create();

    builderOut.readSide = posix_openpt(O_RDWR | O_NOCTTY);
    if (!builderOut.readSide)
        throw SysError("opening pseudoterminal master");

    std::string slaveName(ptsname(builderOut.readSide.get()));

    if (buildUser) {
        if (chmod(slaveName.c_str(), 0600))
            throw SysError("changing mode of pseudoterminal slave");

        if (chown(slaveName.c_str(), buildUser->getUID(), 0))
            throw SysError("changing owner of pseudoterminal slave");
    }
#if __APPLE__
    else {
        if (grantpt(builderOut.readSide.get()))
            throw SysError("granting access to pseudoterminal slave");
    }
#endif

    #if 0
    // Mount the pt in the sandbox so that the "tty" command works.
    // FIXME: this doesn't work with the new devpts in the sandbox.
    if (useChroot)
        dirsInChroot[slaveName] = {slaveName, false};
    #endif

    if (unlockpt(builderOut.readSide.get()))
        throw SysError("unlocking pseudoterminal");

    builderOut.writeSide = open(slaveName.c_str(), O_RDWR | O_NOCTTY);
    if (!builderOut.writeSide)
        throw SysError("opening pseudoterminal slave");

    // Put the pt into raw mode to prevent \n -> \r\n translation.
    struct termios term;
    if (tcgetattr(builderOut.writeSide.get(), &term))
        throw SysError("getting pseudoterminal attributes");

    cfmakeraw(&term);

    if (tcsetattr(builderOut.writeSide.get(), TCSANOW, &term))
        throw SysError("putting pseudoterminal into raw mode");

    result.startTime = time(0);

    /* Fork a child to build the package. */
    ProcessOptions options;

#if __linux__
    if (useChroot) {
        /* Set up private namespaces for the build:

           - The PID namespace causes the build to start as PID 1.
             Processes outside of the chroot are not visible to those
             on the inside, but processes inside the chroot are
             visible from the outside (though with different PIDs).

           - The private mount namespace ensures that all the bind
             mounts we do will only show up in this process and its
             children, and will disappear automatically when we're
             done.

           - The private network namespace ensures that the builder
             cannot talk to the outside world (or vice versa).  It
             only has a private loopback interface. (Fixed-output
             derivations are not run in a private network namespace
             to allow functions like fetchurl to work.)

           - The IPC namespace prevents the builder from communicating
             with outside processes using SysV IPC mechanisms (shared
             memory, message queues, semaphores).  It also ensures
             that all IPC objects are destroyed when the builder
             exits.

           - The UTS namespace ensures that builders see a hostname of
             localhost rather than the actual hostname.

           We use a helper process to do the clone() to work around
           clone() being broken in multi-threaded programs due to
           at-fork handlers not being run. Note that we use
           CLONE_PARENT to ensure that the real builder is parented to
           us.
        */

        if (!(derivationIsImpure(derivationType)))
            privateNetwork = true;

        userNamespaceSync.create();

        options.allowVfork = false;

        Path maxUserNamespaces = "/proc/sys/user/max_user_namespaces";
        static bool userNamespacesEnabled =
            pathExists(maxUserNamespaces)
            && trim(readFile(maxUserNamespaces)) != "0";

        usingUserNamespace = userNamespacesEnabled;

        Pid helper = startProcess([&]() {

            /* Drop additional groups here because we can't do it
               after we've created the new user namespace.  FIXME:
               this means that if we're not root in the parent
               namespace, we can't drop additional groups; they will
               be mapped to nogroup in the child namespace. There does
               not seem to be a workaround for this. (But who can tell
               from reading user_namespaces(7)?)
               See also https://lwn.net/Articles/621612/. */
            if (getuid() == 0 && setgroups(0, 0) == -1)
                throw SysError("setgroups failed");

            size_t stackSize = 1 * 1024 * 1024;
            char * stack = (char *) mmap(0, stackSize,
                PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
            if (stack == MAP_FAILED) throw SysError("allocating stack");

            int flags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS | CLONE_PARENT | SIGCHLD;
            if (privateNetwork)
                flags |= CLONE_NEWNET;
            if (usingUserNamespace)
                flags |= CLONE_NEWUSER;

            pid_t child = clone(childEntry, stack + stackSize, flags, this);
            if (child == -1 && errno == EINVAL) {
                /* Fallback for Linux < 2.13 where CLONE_NEWPID and
                   CLONE_PARENT are not allowed together. */
                flags &= ~CLONE_NEWPID;
                child = clone(childEntry, stack + stackSize, flags, this);
            }
            if (usingUserNamespace && child == -1 && (errno == EPERM || errno == EINVAL)) {
                /* Some distros patch Linux to not allow unprivileged
                 * user namespaces. If we get EPERM or EINVAL, try
                 * without CLONE_NEWUSER and see if that works.
                 */
                usingUserNamespace = false;
                flags &= ~CLONE_NEWUSER;
                child = clone(childEntry, stack + stackSize, flags, this);
            }
            /* Otherwise exit with EPERM so we can handle this in the
               parent. This is only done when sandbox-fallback is set
               to true (the default). */
            if (child == -1 && (errno == EPERM || errno == EINVAL) && settings.sandboxFallback)
                _exit(1);
            if (child == -1) throw SysError("cloning builder process");

            writeFull(builderOut.writeSide.get(),
                fmt("%d %d\n", usingUserNamespace, child));
            _exit(0);
        }, options);

        int res = helper.wait();
        if (res != 0 && settings.sandboxFallback) {
            useChroot = false;
            initTmpDir();
            goto fallback;
        } else if (res != 0)
            throw Error("unable to start build process");

        userNamespaceSync.readSide = -1;

        /* Close the write side to prevent runChild() from hanging
           reading from this. */
        Finally cleanup([&]() {
            userNamespaceSync.writeSide = -1;
        });

        auto ss = tokenizeString<std::vector<std::string>>(readLine(builderOut.readSide.get()));
        assert(ss.size() == 2);
        usingUserNamespace = ss[0] == "1";
        pid = string2Int<pid_t>(ss[1]).value();

        if (usingUserNamespace) {
            /* Set the UID/GID mapping of the builder's user namespace
               such that the sandbox user maps to the build user, or to
               the calling user (if build users are disabled). */
            uid_t hostUid = buildUser ? buildUser->getUID() : getuid();
            uid_t hostGid = buildUser ? buildUser->getGID() : getgid();

            writeFile("/proc/" + std::to_string(pid) + "/uid_map",
                fmt("%d %d 1", sandboxUid(), hostUid));

            writeFile("/proc/" + std::to_string(pid) + "/setgroups", "deny");

            writeFile("/proc/" + std::to_string(pid) + "/gid_map",
                fmt("%d %d 1", sandboxGid(), hostGid));
        } else {
            debug("note: not using a user namespace");
            if (!buildUser)
                throw Error("cannot perform a sandboxed build because user namespaces are not enabled; check /proc/sys/user/max_user_namespaces");
        }

        /* Now that we now the sandbox uid, we can write
           /etc/passwd. */
        writeFile(chrootRootDir + "/etc/passwd", fmt(
                "root:x:0:0:Nix build user:%3%:/noshell\n"
                "nixbld:x:%1%:%2%:Nix build user:%3%:/noshell\n"
                "nobody:x:65534:65534:Nobody:/:/noshell\n",
                sandboxUid(), sandboxGid(), settings.sandboxBuildDir));

        /* Save the mount namespace of the child. We have to do this
           *before* the child does a chroot. */
        sandboxMountNamespace = open(fmt("/proc/%d/ns/mnt", (pid_t) pid).c_str(), O_RDONLY);
        if (sandboxMountNamespace.get() == -1)
            throw SysError("getting sandbox mount namespace");

        /* Signal the builder that we've updated its user namespace. */
        writeFull(userNamespaceSync.writeSide.get(), "1");

    } else
#endif
    {
    fallback:
        options.allowVfork = !buildUser && !drv->isBuiltin();
        pid = startProcess([&]() {
            runChild();
        }, options);
    }

    /* parent */
    pid.setSeparatePG(true);
    builderOut.writeSide = -1;
    worker.childStarted(shared_from_this(), {builderOut.readSide.get()}, true, true);

    /* Check if setting up the build environment failed. */
    std::vector<std::string> msgs;
    while (true) {
        string msg = [&]() {
            try {
                return readLine(builderOut.readSide.get());
            } catch (Error & e) {
                e.addTrace({}, "while waiting for the build environment to initialize (previous messages: %s)",
                    concatStringsSep("|", msgs));
                throw e;
            }
        }();
        if (string(msg, 0, 1) == "\2") break;
        if (string(msg, 0, 1) == "\1") {
            FdSource source(builderOut.readSide.get());
            auto ex = readError(source);
            ex.addTrace({}, "while setting up the build environment");
            throw ex;
        }
        debug("sandbox setup: " + msg);
        msgs.push_back(std::move(msg));
    }
}


void LocalDerivationGoal::initTmpDir() {
    /* In a sandbox, for determinism, always use the same temporary
       directory. */
#if __linux__
    tmpDirInSandbox = useChroot ? settings.sandboxBuildDir : tmpDir;
#else
    tmpDirInSandbox = tmpDir;
#endif

    /* In non-structured mode, add all bindings specified in the
       derivation via the environment, except those listed in the
       passAsFile attribute. Those are passed as file names pointing
       to temporary files containing the contents. Note that
       passAsFile is ignored in structure mode because it's not
       needed (attributes are not passed through the environment, so
       there is no size constraint). */
    if (!parsedDrv->getStructuredAttrs()) {

        StringSet passAsFile = tokenizeString<StringSet>(get(drv->env, "passAsFile").value_or(""));
        for (auto & i : drv->env) {
            if (passAsFile.find(i.first) == passAsFile.end()) {
                env[i.first] = i.second;
            } else {
                auto hash = hashString(htSHA256, i.first);
                string fn = ".attr-" + hash.to_string(Base32, false);
                Path p = tmpDir + "/" + fn;
                writeFile(p, rewriteStrings(i.second, inputRewrites));
                chownToBuilder(p);
                env[i.first + "Path"] = tmpDirInSandbox + "/" + fn;
            }
        }

    }

    /* For convenience, set an environment pointing to the top build
       directory. */
    env["NIX_BUILD_TOP"] = tmpDirInSandbox;

    /* Also set TMPDIR and variants to point to this directory. */
    env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = tmpDirInSandbox;

    /* Explicitly set PWD to prevent problems with chroot builds.  In
       particular, dietlibc cannot figure out the cwd because the
       inode of the current directory doesn't appear in .. (because
       getdents returns the inode of the mount point). */
    env["PWD"] = tmpDirInSandbox;
}


void LocalDerivationGoal::initEnv()
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
    env["NIX_STORE"] = worker.store.storeDir;

    /* The maximum number of cores to utilize for parallel building. */
    env["NIX_BUILD_CORES"] = (format("%d") % settings.buildCores).str();

    initTmpDir();

    /* Compatibility hack with Nix <= 0.7: if this is a fixed-output
       derivation, tell the builder, so that for instance `fetchurl'
       can skip checking the output.  On older Nixes, this environment
       variable won't be set, so `fetchurl' will do the check. */
    if (derivationIsFixed(derivationType)) env["NIX_OUTPUT_CHECKED"] = "1";

    /* *Only* if this is a fixed-output derivation, propagate the
       values of the environment variables specified in the
       `impureEnvVars' attribute to the builder.  This allows for
       instance environment variables for proxy configuration such as
       `http_proxy' to be easily passed to downloaders like
       `fetchurl'.  Passing such environment variables from the caller
       to the builder is generally impure, but the output of
       fixed-output derivations is by definition pure (since we
       already know the cryptographic hash of the output). */
    if (derivationIsImpure(derivationType)) {
        for (auto & i : parsedDrv->getStringsAttr("impureEnvVars").value_or(Strings()))
            env[i] = getEnv(i).value_or("");
    }

    /* Currently structured log messages piggyback on stderr, but we
       may change that in the future. So tell the builder which file
       descriptor to use for that. */
    env["NIX_LOG_FD"] = "2";

    /* Trigger colored output in various tools. */
    env["TERM"] = "xterm-256color";
}


static std::regex shVarName("[A-Za-z_][A-Za-z0-9_]*");


void LocalDerivationGoal::writeStructuredAttrs()
{
    auto structuredAttrs = parsedDrv->getStructuredAttrs();
    if (!structuredAttrs) return;

    auto json = *structuredAttrs;

    /* Add an "outputs" object containing the output paths. */
    nlohmann::json outputs;
    for (auto & i : drv->outputs) {
        /* The placeholder must have a rewrite, so we use it to cover both the
           cases where we know or don't know the output path ahead of time. */
        outputs[i.first] = rewriteStrings(hashPlaceholder(i.first), inputRewrites);
    }
    json["outputs"] = outputs;

    /* Handle exportReferencesGraph. */
    auto e = json.find("exportReferencesGraph");
    if (e != json.end() && e->is_object()) {
        for (auto i = e->begin(); i != e->end(); ++i) {
            std::ostringstream str;
            {
                JSONPlaceholder jsonRoot(str, true);
                StorePathSet storePaths;
                for (auto & p : *i)
                    storePaths.insert(worker.store.parseStorePath(p.get<std::string>()));
                worker.store.pathInfoToJSON(jsonRoot,
                    exportReferences(storePaths), false, true);
            }
            json[i.key()] = nlohmann::json::parse(str.str()); // urgh
        }
    }

    writeFile(tmpDir + "/.attrs.json", rewriteStrings(json.dump(), inputRewrites));
    chownToBuilder(tmpDir + "/.attrs.json");

    /* As a convenience to bash scripts, write a shell file that
       maps all attributes that are representable in bash -
       namely, strings, integers, nulls, Booleans, and arrays and
       objects consisting entirely of those values. (So nested
       arrays or objects are not supported.) */

    auto handleSimpleType = [](const nlohmann::json & value) -> std::optional<std::string> {
        if (value.is_string())
            return shellEscape(value);

        if (value.is_number()) {
            auto f = value.get<float>();
            if (std::ceil(f) == f)
                return std::to_string(value.get<int>());
        }

        if (value.is_null())
            return std::string("''");

        if (value.is_boolean())
            return value.get<bool>() ? std::string("1") : std::string("");

        return {};
    };

    std::string jsonSh;

    for (auto i = json.begin(); i != json.end(); ++i) {

        if (!std::regex_match(i.key(), shVarName)) continue;

        auto & value = i.value();

        auto s = handleSimpleType(value);
        if (s)
            jsonSh += fmt("declare %s=%s\n", i.key(), *s);

        else if (value.is_array()) {
            std::string s2;
            bool good = true;

            for (auto i = value.begin(); i != value.end(); ++i) {
                auto s3 = handleSimpleType(i.value());
                if (!s3) { good = false; break; }
                s2 += *s3; s2 += ' ';
            }

            if (good)
                jsonSh += fmt("declare -a %s=(%s)\n", i.key(), s2);
        }

        else if (value.is_object()) {
            std::string s2;
            bool good = true;

            for (auto i = value.begin(); i != value.end(); ++i) {
                auto s3 = handleSimpleType(i.value());
                if (!s3) { good = false; break; }
                s2 += fmt("[%s]=%s ", shellEscape(i.key()), *s3);
            }

            if (good)
                jsonSh += fmt("declare -A %s=(%s)\n", i.key(), s2);
        }
    }

    writeFile(tmpDir + "/.attrs.sh", rewriteStrings(jsonSh, inputRewrites));
    chownToBuilder(tmpDir + "/.attrs.sh");
}


static StorePath pathPartOfReq(const SingleDerivedPath & req)
{
    return std::visit(overloaded {
        [&](SingleDerivedPath::Opaque bo) {
            return bo.path;
        },
        [&](SingleDerivedPath::Built bfd)  {
            return pathPartOfReq(*bfd.drvPath);
        },
    }, req.raw());
}


static StorePath pathPartOfReq(const DerivedPath & req)
{
    return std::visit(overloaded {
        [&](DerivedPath::Opaque bo) {
            return bo.path;
        },
        [&](DerivedPath::Built bfd)  {
            return pathPartOfReq(*bfd.drvPath);
        },
    }, req.raw());
}


bool LocalDerivationGoal::isAllowed(const DerivedPath & req)
{
    return this->isAllowed(pathPartOfReq(req));
}


struct RestrictedStoreConfig : virtual LocalFSStoreConfig
{
    using LocalFSStoreConfig::LocalFSStoreConfig;
    const std::string name() { return "Restricted Store"; }
};

/* A wrapper around LocalStore that only allows building/querying of
   paths that are in the input closures of the build or were added via
   recursive Nix calls. */
struct RestrictedStore : public virtual RestrictedStoreConfig, public virtual LocalFSStore
{
    ref<LocalStore> next;

    LocalDerivationGoal & goal;

    RestrictedStore(const Params & params, ref<LocalStore> next, LocalDerivationGoal & goal)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
        , RestrictedStoreConfig(params)
        , Store(params)
        , LocalFSStore(params)
        , next(next), goal(goal)
    { }

    Path getRealStoreDir() override
    { return next->realStoreDir; }

    std::string getUri() override
    { return next->getUri(); }

    StorePathSet queryAllValidPaths() override
    {
        StorePathSet paths;
        for (auto & p : goal.inputPaths) paths.insert(p);
        for (auto & p : goal.addedPaths) paths.insert(p);
        return paths;
    }

    void queryPathInfoUncached(const StorePath & path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override
    {
        if (goal.isAllowed(path)) {
            try {
                /* Censor impure information. */
                auto info = std::make_shared<ValidPathInfo>(*next->queryPathInfo(path));
                info->deriver.reset();
                info->registrationTime = 0;
                info->ultimate = false;
                info->sigs.clear();
                callback(info);
            } catch (InvalidPath &) {
                callback(nullptr);
            }
        } else
            callback(nullptr);
    };

    void queryReferrers(const StorePath & path, StorePathSet & referrers) override
    { }

    std::map<std::string, std::optional<StorePath>> queryPartialDerivationOutputMap(const StorePath & path) override
    {
        if (!goal.isAllowed(path))
            throw InvalidPath("cannot query output map for unknown path '%s' in recursive Nix", printStorePath(path));
        return next->queryPartialDerivationOutputMap(path);
    }

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    { throw Error("queryPathFromHashPart"); }

    StorePath addToStore(const string & name, const Path & srcPath,
        FileIngestionMethod method = FileIngestionMethod::Recursive, HashType hashAlgo = htSHA256,
        PathFilter & filter = defaultPathFilter, RepairFlag repair = NoRepair) override
    { throw Error("addToStore"); }

    void addToStore(const ValidPathInfo & info, Source & narSource,
        RepairFlag repair = NoRepair, CheckSigsFlag checkSigs = CheckSigs) override
    {
        next->addToStore(info, narSource, repair, checkSigs);
        goal.addDependency(info.path);
    }

    StorePath addTextToStore(const string & name, const string & s,
        const StorePathSet & references, RepairFlag repair = NoRepair) override
    {
        auto path = next->addTextToStore(name, s, references, repair);
        goal.addDependency(path);
        return path;
    }

    StorePath addToStoreFromDump(Source & dump, const string & name,
        FileIngestionMethod method = FileIngestionMethod::Recursive, HashType hashAlgo = htSHA256, RepairFlag repair = NoRepair) override
    {
        auto path = next->addToStoreFromDump(dump, name, method, hashAlgo, repair);
        goal.addDependency(path);
        return path;
    }

    void narFromPath(const StorePath & path, Sink & sink) override
    {
        if (!goal.isAllowed(path))
            throw InvalidPath("cannot dump unknown path '%s' in recursive Nix", printStorePath(path));
        LocalFSStore::narFromPath(path, sink);
    }

    void ensurePath(const StorePath & path) override
    {
        if (!goal.isAllowed(path))
            throw InvalidPath("cannot substitute unknown path '%s' in recursive Nix", printStorePath(path));
        /* Nothing to be done; 'path' must already be valid. */
    }

    void registerDrvOutput(const Realisation & info) override
    // XXX: This should probably be allowed as a no-op if the realisation
    // corresponds to an allowed derivation
    { throw Error("registerDrvOutput"); }

    std::optional<const Realisation> queryRealisation(const DrvOutput & id) override
    // XXX: This should probably be allowed if the realisation corresponds to
    // an allowed derivation
    { throw Error("queryRealisation"); }

    void buildPaths(const std::vector<DerivedPath> & paths, BuildMode buildMode) override
    {
        if (buildMode != bmNormal) throw Error("unsupported build mode");

        StorePathSet newPaths;

        for (auto & req : paths) {
            if (!goal.isAllowed(req))
                throw InvalidPath("cannot build '%s' in recursive Nix because path is unknown", req.to_string(*next));
        }

        next->buildPaths(paths, buildMode);

        for (auto & path : paths) {
            auto p = std::get_if<DerivedPath::Built>(&path);
            if (!p) continue;
            auto & bfd = *p;
            for (auto & [_, output] : resolveDerivedPath(*next, bfd))
                newPaths.insert(output);
        }

        StorePathSet closure;
        next->computeFSClosure(newPaths, closure);
        for (auto & path : closure)
            goal.addDependency(path);
    }

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
        BuildMode buildMode = bmNormal) override
    { unsupported("buildDerivation"); }

    void addTempRoot(const StorePath & path) override
    { }

    void addIndirectRoot(const Path & path) override
    { }

    Roots findRoots(bool censor) override
    { return Roots(); }

    void collectGarbage(const GCOptions & options, GCResults & results) override
    { }

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override
    { unsupported("addSignatures"); }

    void queryMissing(const std::vector<DerivedPath> & targets,
        StorePathSet & willBuild, StorePathSet & willSubstitute, StorePathSet & unknown,
        uint64_t & downloadSize, uint64_t & narSize) override
    {
        /* This is slightly impure since it leaks information to the
           client about what paths will be built/substituted or are
           already present. Probably not a big deal. */

        std::vector<DerivedPath> allowed;
        for (auto & req : targets) {
            if (goal.isAllowed(req))
                allowed.emplace_back(req);
            else
                unknown.insert(pathPartOfReq(req));
        }

        next->queryMissing(allowed, willBuild, willSubstitute,
            unknown, downloadSize, narSize);
    }
};


void LocalDerivationGoal::startDaemon()
{
    settings.requireExperimentalFeature("recursive-nix");

    Store::Params params;
    params["path-info-cache-size"] = "0";
    params["store"] = worker.store.storeDir;
    params["root"] = getLocalStore().rootDir;
    params["state"] = "/no-such-path";
    params["log"] = "/no-such-path";
    auto store = make_ref<RestrictedStore>(params,
        ref<LocalStore>(std::dynamic_pointer_cast<LocalStore>(worker.store.shared_from_this())),
        *this);

    addedPaths.clear();

    auto socketName = ".nix-socket";
    Path socketPath = tmpDir + "/" + socketName;
    env["NIX_REMOTE"] = "unix://" + tmpDirInSandbox + "/" + socketName;

    daemonSocket = createUnixDomainSocket(socketPath, 0600);

    chownToBuilder(socketPath);

    daemonThread = std::thread([this, store]() {

        while (true) {

            /* Accept a connection. */
            struct sockaddr_un remoteAddr;
            socklen_t remoteAddrLen = sizeof(remoteAddr);

            AutoCloseFD remote = accept(daemonSocket.get(),
                (struct sockaddr *) &remoteAddr, &remoteAddrLen);
            if (!remote) {
                if (errno == EINTR) continue;
                if (errno == EINVAL) break;
                throw SysError("accepting connection");
            }

            closeOnExec(remote.get());

            debug("received daemon connection");

            auto workerThread = std::thread([store, remote{std::move(remote)}]() {
                FdSource from(remote.get());
                FdSink to(remote.get());
                try {
                    daemon::processConnection(store, from, to,
                        daemon::NotTrusted, daemon::Recursive,
                        [&](Store & store) { store.createUser("nobody", 65535); });
                    debug("terminated daemon connection");
                } catch (SysError &) {
                    ignoreException();
                }
            });

            daemonWorkerThreads.push_back(std::move(workerThread));
        }

        debug("daemon shutting down");
    });
}


void LocalDerivationGoal::stopDaemon()
{
    if (daemonSocket && shutdown(daemonSocket.get(), SHUT_RDWR) == -1)
        throw SysError("shutting down daemon socket");

    if (daemonThread.joinable())
        daemonThread.join();

    // FIXME: should prune worker threads more quickly.
    // FIXME: shutdown the client socket to speed up worker termination.
    for (auto & thread : daemonWorkerThreads)
        thread.join();
    daemonWorkerThreads.clear();

    daemonSocket = -1;
}


void LocalDerivationGoal::addDependency(const StorePath & path)
{
    if (isAllowed(path)) return;

    addedPaths.insert(path);

    /* If we're doing a sandbox build, then we have to make the path
       appear in the sandbox. */
    if (useChroot) {

        debug("materialising '%s' in the sandbox", worker.store.printStorePath(path));

        #if __linux__

            Path source = worker.store.Store::toRealPath(path);
            Path target = chrootRootDir + worker.store.printStorePath(path);
            debug("bind-mounting %s -> %s", target, source);

            if (pathExists(target))
                throw Error("store path '%s' already exists in the sandbox", worker.store.printStorePath(path));

            auto st = lstat(source);

            if (S_ISDIR(st.st_mode)) {

                /* Bind-mount the path into the sandbox. This requires
                   entering its mount namespace, which is not possible
                   in multithreaded programs. So we do this in a
                   child process.*/
                Pid child(startProcess([&]() {

                    if (setns(sandboxMountNamespace.get(), 0) == -1)
                        throw SysError("entering sandbox mount namespace");

                    createDirs(target);

                    if (mount(source.c_str(), target.c_str(), "", MS_BIND, 0) == -1)
                        throw SysError("bind mount from '%s' to '%s' failed", source, target);

                    _exit(0);
                }));

                int status = child.wait();
                if (status != 0)
                    throw Error("could not add path '%s' to sandbox", worker.store.printStorePath(path));

            } else
                linkOrCopy(source, target);

        #else
            throw Error("don't know how to make path '%s' (produced by a recursive Nix call) appear in the sandbox",
                worker.store.printStorePath(path));
        #endif

    }
}

void LocalDerivationGoal::chownToBuilder(const Path & path)
{
    if (!buildUser) return;
    if (chown(path.c_str(), buildUser->getUID(), buildUser->getGID()) == -1)
        throw SysError("cannot change ownership of '%1%'", path);
}


void setupSeccomp()
{
#if __linux__
    if (!settings.filterSyscalls) return;
#if HAVE_SECCOMP
    scmp_filter_ctx ctx;

    if (!(ctx = seccomp_init(SCMP_ACT_ALLOW)))
        throw SysError("unable to initialize seccomp mode 2");

    Finally cleanup([&]() {
        seccomp_release(ctx);
    });

    if (nativeSystem == "x86_64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_X86) != 0)
        throw SysError("unable to add 32-bit seccomp architecture");

    if (nativeSystem == "x86_64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_X32) != 0)
        throw SysError("unable to add X32 seccomp architecture");

    if (nativeSystem == "aarch64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_ARM) != 0)
        printError("unable to add ARM seccomp architecture; this may result in spurious build failures if running 32-bit ARM processes");

    /* Prevent builders from creating setuid/setgid binaries. */
    for (int perm : { S_ISUID, S_ISGID }) {
        if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(chmod), 1,
                SCMP_A1(SCMP_CMP_MASKED_EQ, (scmp_datum_t) perm, (scmp_datum_t) perm)) != 0)
            throw SysError("unable to add seccomp rule");

        if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(fchmod), 1,
                SCMP_A1(SCMP_CMP_MASKED_EQ, (scmp_datum_t) perm, (scmp_datum_t) perm)) != 0)
            throw SysError("unable to add seccomp rule");

        if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(fchmodat), 1,
                SCMP_A2(SCMP_CMP_MASKED_EQ, (scmp_datum_t) perm, (scmp_datum_t) perm)) != 0)
            throw SysError("unable to add seccomp rule");
    }

    /* Prevent builders from creating EAs or ACLs. Not all filesystems
       support these, and they're not allowed in the Nix store because
       they're not representable in the NAR serialisation. */
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(setxattr), 0) != 0 ||
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(lsetxattr), 0) != 0 ||
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(fsetxattr), 0) != 0)
        throw SysError("unable to add seccomp rule");

    if (seccomp_attr_set(ctx, SCMP_FLTATR_CTL_NNP, settings.allowNewPrivileges ? 0 : 1) != 0)
        throw SysError("unable to set 'no new privileges' seccomp attribute");

    if (seccomp_load(ctx) != 0)
        throw SysError("unable to load seccomp BPF program");
#else
    throw Error(
        "seccomp is not supported on this platform; "
        "you can bypass this error by setting the option 'filter-syscalls' to false, but note that untrusted builds can then create setuid binaries!");
#endif
#endif
}


void LocalDerivationGoal::runChild()
{
    /* Warning: in the child we should absolutely not make any SQLite
       calls! */

    try { /* child */

        commonChildInit(builderOut);

        try {
            setupSeccomp();
        } catch (...) {
            if (buildUser) throw;
        }

        bool setUser = true;

        /* Make the contents of netrc available to builtin:fetchurl
           (which may run under a different uid and/or in a sandbox). */
        std::string netrcData;
        try {
            if (drv->isBuiltin() && drv->builder == "builtin:fetchurl")
                netrcData = readFile(settings.netrcFile);
        } catch (SysError &) { }

#if __linux__
        if (useChroot) {

            userNamespaceSync.writeSide = -1;

            if (drainFD(userNamespaceSync.readSide.get()) != "1")
                throw Error("user namespace initialisation failed");

            userNamespaceSync.readSide = -1;

            if (privateNetwork) {

                /* Initialise the loopback interface. */
                AutoCloseFD fd(socket(PF_INET, SOCK_DGRAM, IPPROTO_IP));
                if (!fd) throw SysError("cannot open IP socket");

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

            /* Make all filesystems private.  This is necessary
               because subtrees may have been mounted as "shared"
               (MS_SHARED).  (Systemd does this, for instance.)  Even
               though we have a private mount namespace, mounting
               filesystems on top of a shared subtree still propagates
               outside of the namespace.  Making a subtree private is
               local to the namespace, though, so setting MS_PRIVATE
               does not affect the outside world. */
            if (mount(0, "/", 0, MS_PRIVATE | MS_REC, 0) == -1)
                throw SysError("unable to make '/' private");

            /* Bind-mount chroot directory to itself, to treat it as a
               different filesystem from /, as needed for pivot_root. */
            if (mount(chrootRootDir.c_str(), chrootRootDir.c_str(), 0, MS_BIND, 0) == -1)
                throw SysError("unable to bind mount '%1%'", chrootRootDir);

            /* Bind-mount the sandbox's Nix store onto itself so that
               we can mark it as a "shared" subtree, allowing bind
               mounts made in *this* mount namespace to be propagated
               into the child namespace created by the
               unshare(CLONE_NEWNS) call below.

               Marking chrootRootDir as MS_SHARED causes pivot_root()
               to fail with EINVAL. Don't know why. */
            Path chrootStoreDir = chrootRootDir + worker.store.storeDir;

            if (mount(chrootStoreDir.c_str(), chrootStoreDir.c_str(), 0, MS_BIND, 0) == -1)
                throw SysError("unable to bind mount the Nix store", chrootStoreDir);

            if (mount(0, chrootStoreDir.c_str(), 0, MS_SHARED, 0) == -1)
                throw SysError("unable to make '%s' shared", chrootStoreDir);

            /* Set up a nearly empty /dev, unless the user asked to
               bind-mount the host /dev. */
            Strings ss;
            if (dirsInChroot.find("/dev") == dirsInChroot.end()) {
                createDirs(chrootRootDir + "/dev/shm");
                createDirs(chrootRootDir + "/dev/pts");
                ss.push_back("/dev/full");
                if (worker.store.systemFeatures.get().count("kvm") && pathExists("/dev/kvm"))
                    ss.push_back("/dev/kvm");
                ss.push_back("/dev/null");
                ss.push_back("/dev/random");
                ss.push_back("/dev/tty");
                ss.push_back("/dev/urandom");
                ss.push_back("/dev/zero");
                createSymlink("/proc/self/fd", chrootRootDir + "/dev/fd");
                createSymlink("/proc/self/fd/0", chrootRootDir + "/dev/stdin");
                createSymlink("/proc/self/fd/1", chrootRootDir + "/dev/stdout");
                createSymlink("/proc/self/fd/2", chrootRootDir + "/dev/stderr");
            }

            /* Fixed-output derivations typically need to access the
               network, so give them access to /etc/resolv.conf and so
               on. */
            if (derivationIsImpure(derivationType)) {
                // Only use nss functions to resolve hosts and
                // services. Don’t use it for anything else that may
                // be configured for this system. This limits the
                // potential impurities introduced in fixed-outputs.
                writeFile(chrootRootDir + "/etc/nsswitch.conf", "hosts: files dns\nservices: files\n");

                /* N.B. it is realistic that these paths might not exist. It
                   happens when testing Nix building fixed-output derivations
                   within a pure derivation. */
                for (auto & path : { "/etc/resolv.conf", "/etc/services", "/etc/hosts", "/var/run/nscd/socket" })
                    if (pathExists(path))
                        ss.push_back(path);
            }

            for (auto & i : ss) dirsInChroot.emplace(i, i);

            /* Bind-mount all the directories from the "host"
               filesystem that we want in the chroot
               environment. */
            auto doBind = [&](const Path & source, const Path & target, bool optional = false) {
                debug("bind mounting '%1%' to '%2%'", source, target);
                struct stat st;
                if (stat(source.c_str(), &st) == -1) {
                    if (optional && errno == ENOENT)
                        return;
                    else
                        throw SysError("getting attributes of path '%1%'", source);
                }
                if (S_ISDIR(st.st_mode))
                    createDirs(target);
                else {
                    createDirs(dirOf(target));
                    writeFile(target, "");
                }
                if (mount(source.c_str(), target.c_str(), "", MS_BIND | MS_REC, 0) == -1)
                    throw SysError("bind mount from '%1%' to '%2%' failed", source, target);
            };

            for (auto & i : dirsInChroot) {
                if (i.second.source == "/proc") continue; // backwards compatibility
                doBind(i.second.source, chrootRootDir + i.first, i.second.optional);
            }

            /* Bind a new instance of procfs on /proc. */
            createDirs(chrootRootDir + "/proc");
            if (mount("none", (chrootRootDir + "/proc").c_str(), "proc", 0, 0) == -1)
                throw SysError("mounting /proc");

            /* Mount a new tmpfs on /dev/shm to ensure that whatever
               the builder puts in /dev/shm is cleaned up automatically. */
            if (pathExists("/dev/shm") && mount("none", (chrootRootDir + "/dev/shm").c_str(), "tmpfs", 0,
                    fmt("size=%s", settings.sandboxShmSize).c_str()) == -1)
                throw SysError("mounting /dev/shm");

            /* Mount a new devpts on /dev/pts.  Note that this
               requires the kernel to be compiled with
               CONFIG_DEVPTS_MULTIPLE_INSTANCES=y (which is the case
               if /dev/ptx/ptmx exists). */
            if (pathExists("/dev/pts/ptmx") &&
                !pathExists(chrootRootDir + "/dev/ptmx")
                && !dirsInChroot.count("/dev/pts"))
            {
                if (mount("none", (chrootRootDir + "/dev/pts").c_str(), "devpts", 0, "newinstance,mode=0620") == 0)
                {
                    createSymlink("/dev/pts/ptmx", chrootRootDir + "/dev/ptmx");

                    /* Make sure /dev/pts/ptmx is world-writable.  With some
                       Linux versions, it is created with permissions 0.  */
                    chmod_(chrootRootDir + "/dev/pts/ptmx", 0666);
                } else {
                    if (errno != EINVAL)
                        throw SysError("mounting /dev/pts");
                    doBind("/dev/pts", chrootRootDir + "/dev/pts");
                    doBind("/dev/ptmx", chrootRootDir + "/dev/ptmx");
                }
            }

            /* Unshare this mount namespace. This is necessary because
               pivot_root() below changes the root of the mount
               namespace. This means that the call to setns() in
               addDependency() would hide the host's filesystem,
               making it impossible to bind-mount paths from the host
               Nix store into the sandbox. Therefore, we save the
               pre-pivot_root namespace in
               sandboxMountNamespace. Since we made /nix/store a
               shared subtree above, this allows addDependency() to
               make paths appear in the sandbox. */
            if (unshare(CLONE_NEWNS) == -1)
                throw SysError("unsharing mount namespace");

            /* Do the chroot(). */
            if (chdir(chrootRootDir.c_str()) == -1)
                throw SysError("cannot change directory to '%1%'", chrootRootDir);

            if (mkdir("real-root", 0) == -1)
                throw SysError("cannot create real-root directory");

            if (pivot_root(".", "real-root") == -1)
                throw SysError("cannot pivot old root directory onto '%1%'", (chrootRootDir + "/real-root"));

            if (chroot(".") == -1)
                throw SysError("cannot change root directory to '%1%'", chrootRootDir);

            if (umount2("real-root", MNT_DETACH) == -1)
                throw SysError("cannot unmount real root filesystem");

            if (rmdir("real-root") == -1)
                throw SysError("cannot remove real-root directory");

            /* Switch to the sandbox uid/gid in the user namespace,
               which corresponds to the build user or calling user in
               the parent namespace. */
            if (setgid(sandboxGid()) == -1)
                throw SysError("setgid failed");
            if (setuid(sandboxUid()) == -1)
                throw SysError("setuid failed");

            setUser = false;
        }
#endif

        if (chdir(tmpDirInSandbox.c_str()) == -1)
            throw SysError("changing into '%1%'", tmpDir);

        /* Close all other file descriptors. */
        closeMostFDs({STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO});

#if __linux__
        /* Change the personality to 32-bit if we're doing an
           i686-linux build on an x86_64-linux machine. */
        struct utsname utsbuf;
        uname(&utsbuf);
        if (drv->platform == "i686-linux" &&
            (settings.thisSystem == "x86_64-linux" ||
             (!strcmp(utsbuf.sysname, "Linux") && !strcmp(utsbuf.machine, "x86_64")))) {
            if (personality(PER_LINUX32) == -1)
                throw SysError("cannot set i686-linux personality");
        }

        /* Impersonate a Linux 2.6 machine to get some determinism in
           builds that depend on the kernel version. */
        if ((drv->platform == "i686-linux" || drv->platform == "x86_64-linux") && settings.impersonateLinux26) {
            int cur = personality(0xffffffff);
            if (cur != -1) personality(cur | 0x0020000 /* == UNAME26 */);
        }

        /* Disable address space randomization for improved
           determinism. */
        int cur = personality(0xffffffff);
        if (cur != -1) personality(cur | ADDR_NO_RANDOMIZE);
#endif

        /* Disable core dumps by default. */
        struct rlimit limit = { 0, RLIM_INFINITY };
        setrlimit(RLIMIT_CORE, &limit);

        // FIXME: set other limits to deterministic values?

        /* Fill in the environment. */
        Strings envStrs;
        for (auto & i : env)
            envStrs.push_back(rewriteStrings(i.first + "=" + i.second, inputRewrites));

        /* If we are running in `build-users' mode, then switch to the
           user we allocated above.  Make sure that we drop all root
           privileges.  Note that above we have closed all file
           descriptors except std*, so that's safe.  Also note that
           setuid() when run as root sets the real, effective and
           saved UIDs. */
        if (setUser && buildUser) {
            /* Preserve supplementary groups of the build user, to allow
               admins to specify groups such as "kvm".  */
            if (!buildUser->getSupplementaryGIDs().empty() &&
                setgroups(buildUser->getSupplementaryGIDs().size(),
                          buildUser->getSupplementaryGIDs().data()) == -1)
                throw SysError("cannot set supplementary groups of build user");

            if (setgid(buildUser->getGID()) == -1 ||
                getgid() != buildUser->getGID() ||
                getegid() != buildUser->getGID())
                throw SysError("setgid failed");

            if (setuid(buildUser->getUID()) == -1 ||
                getuid() != buildUser->getUID() ||
                geteuid() != buildUser->getUID())
                throw SysError("setuid failed");
        }

        /* Fill in the arguments. */
        Strings args;

        const char *builder = "invalid";

        if (drv->isBuiltin()) {
            ;
        }
#if __APPLE__
        else {
            /* This has to appear before import statements. */
            std::string sandboxProfile = "(version 1)\n";

            if (useChroot) {

                /* Lots and lots and lots of file functions freak out if they can't stat their full ancestry */
                PathSet ancestry;

                /* We build the ancestry before adding all inputPaths to the store because we know they'll
                   all have the same parents (the store), and there might be lots of inputs. This isn't
                   particularly efficient... I doubt it'll be a bottleneck in practice */
                for (auto & i : dirsInChroot) {
                    Path cur = i.first;
                    while (cur.compare("/") != 0) {
                        cur = dirOf(cur);
                        ancestry.insert(cur);
                    }
                }

                /* And we want the store in there regardless of how empty dirsInChroot. We include the innermost
                   path component this time, since it's typically /nix/store and we care about that. */
                Path cur = worker.store.storeDir;
                while (cur.compare("/") != 0) {
                    ancestry.insert(cur);
                    cur = dirOf(cur);
                }

                /* Add all our input paths to the chroot */
                for (auto & i : inputPaths) {
                    auto p = worker.store.printStorePath(i);
                    dirsInChroot[p] = p;
                }

                /* Violations will go to the syslog if you set this. Unfortunately the destination does not appear to be configurable */
                if (settings.darwinLogSandboxViolations) {
                    sandboxProfile += "(deny default)\n";
                } else {
                    sandboxProfile += "(deny default (with no-log))\n";
                }

                sandboxProfile += "(import \"sandbox-defaults.sb\")\n";

                if (derivationIsImpure(derivationType))
                    sandboxProfile += "(import \"sandbox-network.sb\")\n";

                /* Add the output paths we'll use at build-time to the chroot */
                sandboxProfile += "(allow file-read* file-write* process-exec\n";
                for (auto & [_, path] : scratchOutputs)
                    sandboxProfile += fmt("\t(subpath \"%s\")\n", worker.store.printStorePath(path));

                sandboxProfile += ")\n";

                /* Our inputs (transitive dependencies and any impurities computed above)

                   without file-write* allowed, access() incorrectly returns EPERM
                 */
                sandboxProfile += "(allow file-read* file-write* process-exec\n";
                for (auto & i : dirsInChroot) {
                    if (i.first != i.second.source)
                        throw Error(
                            "can't map '%1%' to '%2%': mismatched impure paths not supported on Darwin",
                            i.first, i.second.source);

                    string path = i.first;
                    struct stat st;
                    if (lstat(path.c_str(), &st)) {
                        if (i.second.optional && errno == ENOENT)
                            continue;
                        throw SysError("getting attributes of path '%s", path);
                    }
                    if (S_ISDIR(st.st_mode))
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

                sandboxProfile += additionalSandboxProfile;
            } else
                sandboxProfile += "(import \"sandbox-minimal.sb\")\n";

            debug("Generated sandbox profile:");
            debug(sandboxProfile);

            Path sandboxFile = tmpDir + "/.sandbox.sb";

            writeFile(sandboxFile, sandboxProfile);

            bool allowLocalNetworking = parsedDrv->getBoolAttr("__darwinAllowLocalNetworking");

            /* The tmpDir in scope points at the temporary build directory for our derivation. Some packages try different mechanisms
               to find temporary directories, so we want to open up a broader place for them to dump their files, if needed. */
            Path globalTmpDir = canonPath(getEnv("TMPDIR").value_or("/tmp"), true);

            /* They don't like trailing slashes on subpath directives */
            if (globalTmpDir.back() == '/') globalTmpDir.pop_back();

            if (getEnv("_NIX_TEST_NO_SANDBOX") != "1") {
                builder = "/usr/bin/sandbox-exec";
                args.push_back("sandbox-exec");
                args.push_back("-f");
                args.push_back(sandboxFile);
                args.push_back("-D");
                args.push_back("_GLOBAL_TMP_DIR=" + globalTmpDir);
                args.push_back("-D");
                args.push_back("IMPORT_DIR=" + settings.nixDataDir + "/nix/sandbox/");
                if (allowLocalNetworking) {
                    args.push_back("-D");
                    args.push_back(string("_ALLOW_LOCAL_NETWORKING=1"));
                }
                args.push_back(drv->builder);
            } else {
                builder = drv->builder.c_str();
                args.push_back(std::string(baseNameOf(drv->builder)));
            }
        }
#else
        else {
            builder = drv->builder.c_str();
            args.push_back(std::string(baseNameOf(drv->builder)));
        }
#endif

        for (auto & i : drv->args)
            args.push_back(rewriteStrings(i, inputRewrites));

        /* Indicate that we managed to set up the build environment. */
        writeFull(STDERR_FILENO, string("\2\n"));

        /* Execute the program.  This should not return. */
        if (drv->isBuiltin()) {
            try {
                logger = makeJSONLogger(*logger);

                BasicDerivation & drv2(*drv);
                for (auto & e : drv2.env)
                    e.second = rewriteStrings(e.second, inputRewrites);

                if (drv->builder == "builtin:fetchurl")
                    builtinFetchurl(drv2, netrcData);
                else if (drv->builder == "builtin:buildenv")
                    builtinBuildenv(drv2);
                else if (drv->builder == "builtin:unpack-channel")
                    builtinUnpackChannel(drv2);
                else
                    throw Error("unsupported builtin function '%1%'", string(drv->builder, 8));
                _exit(0);
            } catch (std::exception & e) {
                writeFull(STDERR_FILENO, e.what() + std::string("\n"));
                _exit(1);
            }
        }

#if __APPLE__
        posix_spawnattr_t attrp;

        if (posix_spawnattr_init(&attrp))
            throw SysError("failed to initialize builder");

        if (posix_spawnattr_setflags(&attrp, POSIX_SPAWN_SETEXEC))
            throw SysError("failed to initialize builder");

        if (drv->platform == "aarch64-darwin") {
            // Unset kern.curproc_arch_affinity so we can escape Rosetta
            int affinity = 0;
            sysctlbyname("kern.curproc_arch_affinity", NULL, NULL, &affinity, sizeof(affinity));

            cpu_type_t cpu = CPU_TYPE_ARM64;
            posix_spawnattr_setbinpref_np(&attrp, 1, &cpu, NULL);
        } else if (drv->platform == "x86_64-darwin") {
            cpu_type_t cpu = CPU_TYPE_X86_64;
            posix_spawnattr_setbinpref_np(&attrp, 1, &cpu, NULL);
        }

        posix_spawn(NULL, builder, NULL, &attrp, stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());
#else
        execve(builder, stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());
#endif

        throw SysError("executing '%1%'", drv->builder);

    } catch (Error & e) {
        writeFull(STDERR_FILENO, "\1\n");
        FdSink sink(STDERR_FILENO);
        sink << e;
        sink.flush();
        _exit(1);
    }
}


void LocalDerivationGoal::registerOutputs()
{
    /* When using a build hook, the build hook can register the output
       as valid (by doing `nix-store --import').  If so we don't have
       to do anything here.

       We can only early return when the outputs are known a priori. For
       floating content-addressed derivations this isn't the case.
     */
    if (hook) {
        DerivationGoal::registerOutputs();
        return;
    }

    std::map<std::string, ValidPathInfo> infos;

    /* Set of inodes seen during calls to canonicalisePathMetaData()
       for this build's outputs.  This needs to be shared between
       outputs to allow hard links between outputs. */
    InodesSeen inodesSeen;

    Path checkSuffix = ".check";
    bool keepPreviousRound = settings.keepFailed || settings.runDiffHook;

    std::exception_ptr delayedException;

    /* The paths that can be referenced are the input closures, the
       output paths, and any paths that have been built via recursive
       Nix calls. */
    StorePathSet referenceablePaths;
    for (auto & p : inputPaths) referenceablePaths.insert(p);
    for (auto & i : scratchOutputs) referenceablePaths.insert(i.second);
    for (auto & p : addedPaths) referenceablePaths.insert(p);

    /* FIXME `needsHashRewrite` should probably be removed and we get to the
       real reason why we aren't using the chroot dir */
    auto toRealPathChroot = [&](const Path & p) -> Path {
        return useChroot && !needsHashRewrite()
            ? chrootRootDir + p
            : worker.store.toRealPath(p);
    };

    /* Check whether the output paths were created, and make all
       output paths read-only.  Then get the references of each output (that we
       might need to register), so we can topologically sort them. For the ones
       that are most definitely already installed, we just store their final
       name so we can also use it in rewrites. */
    StringSet outputsToSort;
    struct AlreadyRegistered { StorePath path; };
    struct PerhapsNeedToRegister { StorePathSet refs; };
    std::map<std::string, std::variant<AlreadyRegistered, PerhapsNeedToRegister>> outputReferencesIfUnregistered;
    std::map<std::string, struct stat> outputStats;
    for (auto & [outputName, _] : drv->outputs) {
        auto actualPath = toRealPathChroot(worker.store.printStorePath(scratchOutputs.at(outputName)));

        outputsToSort.insert(outputName);

        /* Updated wanted info to remove the outputs we definitely don't need to register */
        auto & initialInfo = initialOutputs.at(outputName);

        /* Don't register if already valid, and not checking */
        initialInfo.wanted = buildMode == bmCheck
            || !(initialInfo.known && initialInfo.known->isValid());
        if (!initialInfo.wanted) {
            outputReferencesIfUnregistered.insert_or_assign(
                outputName,
                AlreadyRegistered { .path = initialInfo.known->path });
            continue;
        }

        struct stat st;
        if (lstat(actualPath.c_str(), &st) == -1) {
            if (errno == ENOENT)
                throw BuildError(
                    "builder for '%s' failed to produce output path for output '%s' at '%s'",
                    worker.store.printStorePath(drvPath), outputName, actualPath);
            throw SysError("getting attributes of path '%s'", actualPath);
        }

#ifndef __CYGWIN__
        /* Check that the output is not group or world writable, as
           that means that someone else can have interfered with the
           build.  Also, the output should be owned by the build
           user. */
        if ((!S_ISLNK(st.st_mode) && (st.st_mode & (S_IWGRP | S_IWOTH))) ||
            (buildUser && st.st_uid != buildUser->getUID()))
            throw BuildError(
                    "suspicious ownership or permission on '%s' for output '%s'; rejecting this build output",
                    actualPath, outputName);
#endif

        /* Canonicalise first.  This ensures that the path we're
           rewriting doesn't contain a hard link to /etc/shadow or
           something like that. */
        canonicalisePathMetaData(actualPath, buildUser ? buildUser->getUID() : -1, inodesSeen);

        debug("scanning for references for output '%s' in temp location '%s'", outputName, actualPath);

        /* Pass blank Sink as we are not ready to hash data at this stage. */
        NullSink blank;
        auto references = worker.store.parseStorePathSet(
            scanForReferences(blank, actualPath, worker.store.printStorePathSet(referenceablePaths)));

        outputReferencesIfUnregistered.insert_or_assign(
            outputName,
            PerhapsNeedToRegister { .refs = references });
        outputStats.insert_or_assign(outputName, std::move(st));
    }

    auto sortedOutputNames = topoSort(outputsToSort,
        {[&](const std::string & name) {
            return std::visit(overloaded {
                /* Since we'll use the already installed versions of these, we
                   can treat them as leaves and ignore any references they
                   have. */
                [&](AlreadyRegistered _) { return StringSet {}; },
                [&](PerhapsNeedToRegister refs) {
                    StringSet referencedOutputs;
                    /* FIXME build inverted map up front so no quadratic waste here */
                    for (auto & r : refs.refs)
                        for (auto & [o, p] : scratchOutputs)
                            if (r == p)
                                referencedOutputs.insert(o);
                    return referencedOutputs;
                },
            }, outputReferencesIfUnregistered.at(name));
        }},
        {[&](const std::string & path, const std::string & parent) {
            // TODO with more -vvvv also show the temporary paths for manual inspection.
            return BuildError(
                "cycle detected in build of '%s' in the references of output '%s' from output '%s'",
                worker.store.printStorePath(drvPath), path, parent);
        }});

    std::reverse(sortedOutputNames.begin(), sortedOutputNames.end());

    for (auto & outputName : sortedOutputNames) {
        auto output = drv->outputs.at(outputName);
        auto & scratchPath = scratchOutputs.at(outputName);
        auto actualPath = toRealPathChroot(worker.store.printStorePath(scratchPath));

        auto finish = [&](StorePath finalStorePath) {
            /* Store the final path */
            finalOutputs.insert_or_assign(outputName, finalStorePath);
            /* The rewrite rule will be used in downstream outputs that refer to
               use. This is why the topological sort is essential to do first
               before this for loop. */
            if (scratchPath != finalStorePath)
                outputRewrites[std::string { scratchPath.hashPart() }] = std::string { finalStorePath.hashPart() };
        };

        std::optional<StorePathSet> referencesOpt = std::visit(overloaded {
            [&](AlreadyRegistered skippedFinalPath) -> std::optional<StorePathSet> {
                finish(skippedFinalPath.path);
                return std::nullopt;
            },
            [&](PerhapsNeedToRegister r) -> std::optional<StorePathSet> {
                return r.refs;
            },
        }, outputReferencesIfUnregistered.at(outputName));

        if (!referencesOpt)
            continue;
        auto references = *referencesOpt;

        auto rewriteOutput = [&]() {
            /* Apply hash rewriting if necessary. */
            if (!outputRewrites.empty()) {
                warn("rewriting hashes in '%1%'; cross fingers", actualPath);

                /* FIXME: this is in-memory. */
                StringSink sink;
                dumpPath(actualPath, sink);
                deletePath(actualPath);
                sink.s = make_ref<std::string>(rewriteStrings(*sink.s, outputRewrites));
                StringSource source(*sink.s);
                restorePath(actualPath, source);

                /* FIXME: set proper permissions in restorePath() so
                   we don't have to do another traversal. */
                canonicalisePathMetaData(actualPath, -1, inodesSeen);
            }
        };

        auto rewriteRefs = [&]() -> PathReferences<StorePath> {
            /* In the CA case, we need the rewritten refs to calculate the
               final path, therefore we look for a *non-rewritten
               self-reference, and use a bool rather try to solve the
               computationally intractable fixed point. */
            PathReferences<StorePath> res {
                .hasSelfReference = false,
            };
            for (auto & r : references) {
                auto name = r.name();
                auto origHash = std::string { r.hashPart() };
                if (r == scratchPath)
                    res.hasSelfReference = true;
                else if (outputRewrites.count(origHash) == 0)
                    res.references.insert(r);
                else {
                    std::string newRef = outputRewrites.at(origHash);
                    newRef += '-';
                    newRef += name;
                    res.references.insert(StorePath { newRef });
                }
            }
            return res;
        };

        auto newInfoFromCA = [&](const DerivationOutputCAFloating outputHash) -> ValidPathInfo {
            auto & st = outputStats.at(outputName);
            if (outputHash.method == ContentAddressMethod { FileIngestionMethod::Flat } ||
                outputHash.method == ContentAddressMethod { TextHashMethod {} })
            {
                /* The output path should be a regular file without execute permission. */
                if (!S_ISREG(st.st_mode) || (st.st_mode & S_IXUSR) != 0)
                    throw BuildError(
                        "output path '%1%' should be a non-executable regular file "
                        "since recursive hashing is not enabled (one of outputHashMode={flat,text} is true)",
                        actualPath);
            }
            rewriteOutput();
            /* FIXME optimize and deduplicate with addToStore */
            std::string oldHashPart { scratchPath.hashPart() };
            HashModuloSink caSink { outputHash.hashType, oldHashPart };
            std::visit(overloaded {
                [&](TextHashMethod _) {
                    readFile(actualPath, caSink);
                },
                [&](FileIngestionMethod m2) {
                    switch (m2) {
                    case FileIngestionMethod::Recursive:
                        dumpPath(actualPath, caSink);
                        break;
                    case FileIngestionMethod::Flat:
                        readFile(actualPath, caSink);
                        break;
                    }
                },
            }, outputHash.method);
            auto got = caSink.finish().first;
            HashModuloSink narSink { htSHA256, oldHashPart };
            dumpPath(actualPath, narSink);
            auto narHashAndSize = narSink.finish();
            ValidPathInfo newInfo0 {
                worker.store,
                {
                    .name = outputPathName(drv->name, outputName),
                    .info = contentAddressFromMethodHashAndRefs(
                        outputHash.method,
                        std::move(got),
                        rewriteRefs()),
                },
                narHashAndSize.first,
            };
            newInfo0.narSize = narHashAndSize.second;
            if (scratchPath != newInfo0.path) {
                // Also rewrite the output path
                auto source = sinkToSource([&](Sink & nextSink) {
                    StringSink sink;
                    dumpPath(actualPath, sink);
                    RewritingSink rsink2(oldHashPart, std::string(newInfo0.path.hashPart()), nextSink);
                    rsink2(*sink.s);
                    rsink2.flush();
                });
                Path tmpPath = actualPath + ".tmp";
                restorePath(tmpPath, *source);
                deletePath(actualPath);
                movePath(tmpPath, actualPath);
            }

            assert(newInfo0.ca);
            return newInfo0;
        };

        ValidPathInfo newInfo = std::visit(overloaded {
            [&](DerivationOutputInputAddressed output) {
                /* input-addressed case */
                auto requiredFinalPath = output.path;
                /* Preemptively add rewrite rule for final hash, as that is
                   what the NAR hash will use rather than normalized-self references */
                if (scratchPath != requiredFinalPath)
                    outputRewrites.insert_or_assign(
                        std::string { scratchPath.hashPart() },
                        std::string { requiredFinalPath.hashPart() });
                rewriteOutput();
                auto narHashAndSize = hashPath(htSHA256, actualPath);
                ValidPathInfo newInfo0 { requiredFinalPath, narHashAndSize.first };
                newInfo0.narSize = narHashAndSize.second;
                static_cast<PathReferences<StorePath> &>(newInfo0) = rewriteRefs();
                return newInfo0;
            },
            [&](DerivationOutputCAFixed dof) {
                auto wanted = getContentAddressHash(dof.ca);

                auto newInfo0 = newInfoFromCA(DerivationOutputCAFloating {
                    .method = getContentAddressMethod(dof.ca),
                    .hashType = wanted.type,
                });

                /* Check wanted hash */
                assert(newInfo0.ca);
                auto got = getContentAddressHash(*newInfo0.ca);
                if (wanted != got) {
                    /* Throw an error after registering the path as
                       valid. */
                    worker.hashMismatch = true;
                    delayedException = std::make_exception_ptr(
                        BuildError("hash mismatch in fixed-output derivation '%s':\n  specified: %s\n     got:    %s",
                            worker.store.printStorePath(drvPath),
                            wanted.to_string(SRI, true),
                            got.to_string(SRI, true)));
                }
                if (static_cast<const PathReferences<StorePath> &>(newInfo0) != PathReferences<StorePath> {})
                    delayedException = std::make_exception_ptr(
                        BuildError("illegal path references in fixed-output derivation '%s'",
                            worker.store.printStorePath(drvPath)));

                return newInfo0;
            },
            [&](DerivationOutputCAFloating dof) {
                return newInfoFromCA(dof);
            },
                [&](DerivationOutputDeferred) {
                // No derivation should reach that point without having been
                // rewritten first
                assert(false);
                // Ugly, but the compiler insists on having this return a value
                // of type `ValidPathInfo` despite the `assert(false)`, so
                // let's provide it
                return *(ValidPathInfo*)0;
            },
        }, output.output);

        /* Calculate where we'll move the output files. In the checking case we
           will leave leave them where they are, for now, rather than move to
           their usual "final destination" */
        auto finalDestPath = worker.store.printStorePath(newInfo.path);

        /* Lock final output path, if not already locked. This happens with
           floating CA derivations and hash-mismatching fixed-output
           derivations. */
        PathLocks dynamicOutputLock;
        auto optFixedPath = output.path(worker.store, drv->name, outputName);
        if (!optFixedPath ||
            worker.store.printStorePath(*optFixedPath) != finalDestPath)
        {
            assert(newInfo.ca);
            dynamicOutputLock.lockPaths({worker.store.toRealPath(finalDestPath)});
        }

        /* Move files, if needed */
        if (worker.store.toRealPath(finalDestPath) != actualPath) {
            if (buildMode == bmRepair) {
                /* Path already exists, need to replace it */
                replaceValidPath(worker.store.toRealPath(finalDestPath), actualPath);
                actualPath = worker.store.toRealPath(finalDestPath);
            } else if (buildMode == bmCheck) {
                /* Path already exists, and we want to compare, so we leave out
                   new path in place. */
            } else if (worker.store.isValidPath(newInfo.path)) {
                /* Path already exists because CA path produced by something
                   else. No moving needed. */
                assert(newInfo.ca);
            } else {
                auto destPath = worker.store.toRealPath(finalDestPath);
                movePath(actualPath, destPath);
                actualPath = destPath;
            }
        }

        auto & localStore = getLocalStore();

        if (buildMode == bmCheck) {

            if (!worker.store.isValidPath(newInfo.path)) continue;
            ValidPathInfo oldInfo(*worker.store.queryPathInfo(newInfo.path));
            if (newInfo.narHash != oldInfo.narHash) {
                worker.checkMismatch = true;
                if (settings.runDiffHook || settings.keepFailed) {
                    auto dst = worker.store.toRealPath(finalDestPath + checkSuffix);
                    deletePath(dst);
                    movePath(actualPath, dst);

                    handleDiffHook(
                        buildUser ? buildUser->getUID() : getuid(),
                        buildUser ? buildUser->getGID() : getgid(),
                        finalDestPath, dst, worker.store.printStorePath(drvPath), tmpDir);

                    throw NotDeterministic("derivation '%s' may not be deterministic: output '%s' differs from '%s'",
                        worker.store.printStorePath(drvPath), worker.store.toRealPath(finalDestPath), dst);
                } else
                    throw NotDeterministic("derivation '%s' may not be deterministic: output '%s' differs",
                        worker.store.printStorePath(drvPath), worker.store.toRealPath(finalDestPath));
            }

            /* Since we verified the build, it's now ultimately trusted. */
            if (!oldInfo.ultimate) {
                oldInfo.ultimate = true;
                localStore.signPathInfo(oldInfo);
                localStore.registerValidPaths({{oldInfo.path, oldInfo}});
            }

            continue;
        }

        /* For debugging, print out the referenced and unreferenced paths. */
        for (auto & i : inputPaths) {
            auto j = references.find(i);
            if (j == references.end())
                debug("unreferenced input: '%1%'", worker.store.printStorePath(i));
            else
                debug("referenced input: '%1%'", worker.store.printStorePath(i));
        }

        if (curRound == nrRounds) {
            localStore.optimisePath(actualPath); // FIXME: combine with scanForReferences()
            worker.markContentsGood(newInfo.path);
        }

        newInfo.deriver = drvPath;
        newInfo.ultimate = true;
        localStore.signPathInfo(newInfo);

        finish(newInfo.path);

        /* If it's a CA path, register it right away. This is necessary if it
           isn't statically known so that we can safely unlock the path before
           the next iteration */
        if (newInfo.ca)
            localStore.registerValidPaths({{newInfo.path, newInfo}});

        infos.emplace(outputName, std::move(newInfo));
    }

    if (buildMode == bmCheck) return;

    /* Apply output checks. */
    checkOutputs(infos);

    /* Compare the result with the previous round, and report which
       path is different, if any.*/
    if (curRound > 1 && prevInfos != infos) {
        assert(prevInfos.size() == infos.size());
        for (auto i = prevInfos.begin(), j = infos.begin(); i != prevInfos.end(); ++i, ++j)
            if (!(*i == *j)) {
                result.isNonDeterministic = true;
                Path prev = worker.store.printStorePath(i->second.path) + checkSuffix;
                bool prevExists = keepPreviousRound && pathExists(prev);
                hintformat hint = prevExists
                    ? hintfmt("output '%s' of '%s' differs from '%s' from previous round",
                        worker.store.printStorePath(i->second.path), worker.store.printStorePath(drvPath), prev)
                    : hintfmt("output '%s' of '%s' differs from previous round",
                        worker.store.printStorePath(i->second.path), worker.store.printStorePath(drvPath));

                handleDiffHook(
                    buildUser ? buildUser->getUID() : getuid(),
                    buildUser ? buildUser->getGID() : getgid(),
                    prev, worker.store.printStorePath(i->second.path),
                    worker.store.printStorePath(drvPath), tmpDir);

                if (settings.enforceDeterminism)
                    throw NotDeterministic(hint);

                printError(hint);

                curRound = nrRounds; // we know enough, bail out early
            }
    }

    /* If this is the first round of several, then move the output out of the way. */
    if (nrRounds > 1 && curRound == 1 && curRound < nrRounds && keepPreviousRound) {
        for (auto & [_, outputStorePath] : finalOutputs) {
            auto path = worker.store.printStorePath(outputStorePath);
            Path prev = path + checkSuffix;
            deletePath(prev);
            Path dst = path + checkSuffix;
            if (rename(path.c_str(), dst.c_str()))
                throw SysError("renaming '%s' to '%s'", path, dst);
        }
    }

    if (curRound < nrRounds) {
        prevInfos = std::move(infos);
        return;
    }

    /* Remove the .check directories if we're done. FIXME: keep them
       if the result was not determistic? */
    if (curRound == nrRounds) {
        for (auto & [_, outputStorePath] : finalOutputs) {
            Path prev = worker.store.printStorePath(outputStorePath) + checkSuffix;
            deletePath(prev);
        }
    }

    /* Register each output path as valid, and register the sets of
       paths referenced by each of them.  If there are cycles in the
       outputs, this will fail. */
    {
        auto & localStore = getLocalStore();

        ValidPathInfos infos2;
        for (auto & [outputName, newInfo] : infos) {
            infos2.insert_or_assign(newInfo.path, newInfo);
        }
        localStore.registerValidPaths(infos2);
    }

    /* In case of a fixed-output derivation hash mismatch, throw an
       exception now that we have registered the output as valid. */
    if (delayedException)
        std::rethrow_exception(delayedException);

    /* If we made it this far, we are sure the output matches the derivation
       (since the delayedException would be a fixed output CA mismatch). That
       means it's safe to link the derivation to the output hash. We must do
       that for floating CA derivations, which otherwise couldn't be cached,
       but it's fine to do in all cases. */

    if (settings.isExperimentalFeatureEnabled("ca-derivations")) {
        for (auto& [outputName, newInfo] : infos) {
            auto thisRealisation = Realisation{
                .id = DrvOutput{initialOutputs.at(outputName).outputHash,
                                outputName},
                .outPath = newInfo.path};
            signRealisation(thisRealisation);
            worker.store.registerDrvOutput(thisRealisation);
        }
    }
}

void LocalDerivationGoal::signRealisation(Realisation & realisation)
{
    getLocalStore().signRealisation(realisation);
}


void LocalDerivationGoal::checkOutputs(const std::map<Path, ValidPathInfo> & outputs)
{
    std::map<Path, const ValidPathInfo &> outputsByPath;
    for (auto & output : outputs)
        outputsByPath.emplace(worker.store.printStorePath(output.second.path), output.second);

    for (auto & output : outputs) {
        auto & outputName = output.first;
        auto & info = output.second;

        struct Checks
        {
            bool ignoreSelfRefs = false;
            std::optional<uint64_t> maxSize, maxClosureSize;
            std::optional<Strings> allowedReferences, allowedRequisites, disallowedReferences, disallowedRequisites;
        };

        /* Compute the closure and closure size of some output. This
           is slightly tricky because some of its references (namely
           other outputs) may not be valid yet. */
        auto getClosure = [&](const StorePath & path)
        {
            uint64_t closureSize = 0;
            StorePathSet pathsDone;
            std::queue<StorePath> pathsLeft;
            pathsLeft.push(path);

            while (!pathsLeft.empty()) {
                auto path = pathsLeft.front();
                pathsLeft.pop();
                if (!pathsDone.insert(path).second) continue;

                auto i = outputsByPath.find(worker.store.printStorePath(path));
                if (i != outputsByPath.end()) {
                    closureSize += i->second.narSize;
                    for (auto & ref : i->second.referencesPossiblyToSelf())
                        pathsLeft.push(ref);
                } else {
                    auto info = worker.store.queryPathInfo(path);
                    closureSize += info->narSize;
                    for (auto & ref : info->referencesPossiblyToSelf())
                        pathsLeft.push(ref);
                }
            }

            return std::make_pair(std::move(pathsDone), closureSize);
        };

        auto applyChecks = [&](const Checks & checks)
        {
            if (checks.maxSize && info.narSize > *checks.maxSize)
                throw BuildError("path '%s' is too large at %d bytes; limit is %d bytes",
                    worker.store.printStorePath(info.path), info.narSize, *checks.maxSize);

            if (checks.maxClosureSize) {
                uint64_t closureSize = getClosure(info.path).second;
                if (closureSize > *checks.maxClosureSize)
                    throw BuildError("closure of path '%s' is too large at %d bytes; limit is %d bytes",
                        worker.store.printStorePath(info.path), closureSize, *checks.maxClosureSize);
            }

            auto checkRefs = [&](const std::optional<Strings> & value, bool allowed, bool recursive)
            {
                if (!value) return;

                /* Parse a list of reference specifiers.  Each element must
                   either be a store path, or the symbolic name of the output
                   of the derivation (such as `out'). */
                StorePathSet spec;
                for (auto & i : *value) {
                    if (worker.store.isStorePath(i))
                        spec.insert(worker.store.parseStorePath(i));
                    else if (finalOutputs.count(i))
                        spec.insert(finalOutputs.at(i));
                    else throw BuildError("derivation contains an illegal reference specifier '%s'", i);
                }

                auto used = recursive
                    ? getClosure(info.path).first
                    : info.referencesPossiblyToSelf();

                if (recursive && checks.ignoreSelfRefs)
                    used.erase(info.path);

                StorePathSet badPaths;

                for (auto & i : used)
                    if (allowed) {
                        if (!spec.count(i))
                            badPaths.insert(i);
                    } else {
                        if (spec.count(i))
                            badPaths.insert(i);
                    }

                if (!badPaths.empty()) {
                    string badPathsStr;
                    for (auto & i : badPaths) {
                        badPathsStr += "\n  ";
                        badPathsStr += worker.store.printStorePath(i);
                    }
                    throw BuildError("output '%s' is not allowed to refer to the following paths:%s",
                        worker.store.printStorePath(info.path), badPathsStr);
                }
            };

            checkRefs(checks.allowedReferences, true, false);
            checkRefs(checks.allowedRequisites, true, true);
            checkRefs(checks.disallowedReferences, false, false);
            checkRefs(checks.disallowedRequisites, false, true);
        };

        if (auto structuredAttrs = parsedDrv->getStructuredAttrs()) {
            auto outputChecks = structuredAttrs->find("outputChecks");
            if (outputChecks != structuredAttrs->end()) {
                auto output = outputChecks->find(outputName);

                if (output != outputChecks->end()) {
                    Checks checks;

                    auto maxSize = output->find("maxSize");
                    if (maxSize != output->end())
                        checks.maxSize = maxSize->get<uint64_t>();

                    auto maxClosureSize = output->find("maxClosureSize");
                    if (maxClosureSize != output->end())
                        checks.maxClosureSize = maxClosureSize->get<uint64_t>();

                    auto get = [&](const std::string & name) -> std::optional<Strings> {
                        auto i = output->find(name);
                        if (i != output->end()) {
                            Strings res;
                            for (auto j = i->begin(); j != i->end(); ++j) {
                                if (!j->is_string())
                                    throw Error("attribute '%s' of derivation '%s' must be a list of strings", name, worker.store.printStorePath(drvPath));
                                res.push_back(j->get<std::string>());
                            }
                            checks.disallowedRequisites = res;
                            return res;
                        }
                        return {};
                    };

                    checks.allowedReferences = get("allowedReferences");
                    checks.allowedRequisites = get("allowedRequisites");
                    checks.disallowedReferences = get("disallowedReferences");
                    checks.disallowedRequisites = get("disallowedRequisites");

                    applyChecks(checks);
                }
            }
        } else {
            // legacy non-structured-attributes case
            Checks checks;
            checks.ignoreSelfRefs = true;
            checks.allowedReferences = parsedDrv->getStringsAttr("allowedReferences");
            checks.allowedRequisites = parsedDrv->getStringsAttr("allowedRequisites");
            checks.disallowedReferences = parsedDrv->getStringsAttr("disallowedReferences");
            checks.disallowedRequisites = parsedDrv->getStringsAttr("disallowedRequisites");
            applyChecks(checks);
        }
    }
}


void LocalDerivationGoal::deleteTmpDir(bool force)
{
    if (tmpDir != "") {
        /* Don't keep temporary directories for builtins because they
           might have privileged stuff (like a copy of netrc). */
        if (settings.keepFailed && !force && !drv->isBuiltin()) {
            printError("note: keeping build directory '%s'", tmpDir);
            chmod(tmpDir.c_str(), 0755);
        }
        else
            deletePath(tmpDir);
        tmpDir = "";
    }
}


bool LocalDerivationGoal::isReadDesc(int fd)
{
    return (hook && DerivationGoal::isReadDesc(fd)) ||
        (!hook && fd == builderOut.readSide.get());
}


StorePath LocalDerivationGoal::makeFallbackPath(std::string_view outputName)
{
    return worker.store.makeStorePath(
        "rewrite:" + std::string(drvPath.to_string()) + ":name:" + std::string(outputName),
        Hash(htSHA256), outputPathName(drv->name, outputName));
}


StorePath LocalDerivationGoal::makeFallbackPath(const StorePath & path)
{
    return worker.store.makeStorePath(
        "rewrite:" + std::string(drvPath.to_string()) + ":" + std::string(path.to_string()),
        Hash(htSHA256), path.name());
}


}
