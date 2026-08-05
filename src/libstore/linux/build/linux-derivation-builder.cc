#include "linux-derivation-builder.hh"
#include "derivation-builder-impl.hh"
#include "chroot-derivation-builder.hh"
#include "chroot-linux-derivation-builder.hh"

#include "store-config-private.hh"

#include "nix/store/globals.hh"
#include "nix/store/personality.hh"
#include "nix/store/filetransfer.hh"
#include "nix/util/cgroup.hh"
#include "nix/util/linux-namespaces.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/logging.hh"
#include "nix/util/serialise.hh"
#include "linux/fchmodat2-compat.hh"

#include <algorithm>
#include <string_view>
#include <cstdint>
#include <atomic>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <sys/mman.h>
#include <sched.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <grp.h>

#if HAVE_SECCOMP
#  include <seccomp.h>
#endif

#define pivot_root(new_root, put_old) (syscall(SYS_pivot_root, new_root, put_old))

/* Polyfill for musl that doesn't have the syscall wrappers. */

#if !HAVE_OPEN_TREE

#  ifndef OPEN_TREE_CLONE
#    define OPEN_TREE_CLONE 1
#  endif

#  ifndef OPEN_TREE_CLOEXEC
#    define OPEN_TREE_CLOEXEC O_CLOEXEC
#  endif

static int open_tree(int dirfd, const char * filename, unsigned int flags)
{
    return ::syscall(__NR_open_tree, dirfd, filename, flags);
}

#endif

#if !HAVE_MOVE_MOUNT

#  ifndef MOVE_MOUNT_F_EMPTY_PATH
#    define MOVE_MOUNT_F_EMPTY_PATH 0x00000004
#  endif

#  ifndef MOVE_MOUNT_T_EMPTY_PATH
#    define MOVE_MOUNT_T_EMPTY_PATH 0x00000040
#  endif

static int move_mount(int sourceDirFd, const char * source, int destDirFd, const char * dest, unsigned int flags)
{
    return ::syscall(__NR_move_mount, sourceDirFd, source, destDirFd, dest, flags);
}

#endif

namespace nix {

void setupSeccomp(const LocalSettings & localSettings)
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

    /* Prevent builders from using EAs or ACLs. Not all filesystems
       support these, and they're not allowed in the Nix store because
       they're not representable in the NAR serialisation. */
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(listxattr), 0) != 0
        || seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(llistxattr), 0) != 0
        || seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(flistxattr), 0) != 0
        || seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(getxattr), 0) != 0
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

#if DO_LANDLOCK

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

#endif

static void doBind(
    const std::filesystem::path & source,
    Descriptor chrootRootDirFd,
    const std::filesystem::path & target,
    const std::filesystem::path & chrootRootDirPath,
    bool optional = false)
{
    /* `target` denotes a relative path inside the chroot directory. All operations happen
        relative to chrootRootDirFd. Bail out if the path is not what we expect. */
    if (target.empty() || target.is_absolute())
        throw Error("invalid path to bind mount in the chroot: %s", PathFmt(target));

    /* Sanity check against insane setups. This would never be passed in
       sandbox paths for dependencies, but the user might specify it in
       extra-sandbox-paths. */
    if (const auto filename = target.filename().native(); filename == "." || filename == "..")
        throw Error("sandbox path to bind mount in the chroot has an invalid filename: %s", PathFmt(target));

    debug("bind mounting %1% to %2%", PathFmt(source), PathFmt(chrootRootDirPath / target));

    auto fallbackBindMount = [&](Descriptor sourceFd, Descriptor destFd) {
        auto selfProcSourcePath = std::filesystem::path("/proc/self/fd") / std::to_string(sourceFd);
        auto selfProcDestPath = std::filesystem::path("/proc/self/fd") / std::to_string(destFd);

        if (mount(selfProcSourcePath.c_str(), selfProcDestPath.c_str(), "", MS_BIND | MS_REC, 0) == -1)
            throw SysError(
                "bind mount from %1% (%3%) to %2% (%4%) failed",
                PathFmt(source),
                PathFmt(chrootRootDirPath / target),
                PathFmt(selfProcSourcePath),
                PathFmt(selfProcDestPath));
    };

    auto bindMount = [&](Descriptor sourceFd, Descriptor destFd) {
        static std::atomic_flag openTreeUnsupported{};
        if (openTreeUnsupported.test())
            return fallbackBindMount(sourceFd, destFd);

        AutoCloseFD treeFd =
            ::open_tree(sourceFd, "", OPEN_TREE_CLONE | AT_RECURSIVE | OPEN_TREE_CLOEXEC | AT_EMPTY_PATH);

        if (!treeFd) {
            if (errno != ENOSYS)
                throw SysError("opening bind mount source %1%", PathFmt(source));

            /* Cache and try the old fallback via procfs. */
            openTreeUnsupported.test_and_set();
            return fallbackBindMount(sourceFd, destFd);
        }

        if (::move_mount(treeFd.get(), "", destFd, "", MOVE_MOUNT_T_EMPTY_PATH | MOVE_MOUNT_F_EMPTY_PATH) == -1)
            throw SysError("bind mount from %1% to %2% failed", PathFmt(source), PathFmt(chrootRootDirPath / target));
    };

    /* TODO: Replace the sucky implementation of createDirs and use that instead. */
    auto createDirsAndOpen =
        [&](const std::filesystem::path & path) -> std::tuple<AutoCloseFD, Descriptor, bool /*isRoot*/> {
        Descriptor parentFd = chrootRootDirFd;
        AutoCloseFD maybeParentFdOwned;
        /* How many directories deep we are. */
        unsigned nestedDirCount = 0;

        for (const auto & p : path) {
            /* Trailing `/`, we don't care. It might matter for symlink resolution, so better skip it.
               Also skip `.` as a no-op. */
            if (p.native().empty() || p.native() == ".")
                continue;

            /* TODO: Add additional validation in config parsing in case the
               user specified a borked `extra-sandbox-paths`. Maybe we should just
               reject `..` components completely? */
            if (p.native() == "..") {
                if (nestedDirCount == 0)
                    throw Error("sandbox path %s escapes the chroot", PathFmt(path));
                --nestedDirCount;
            } else {
                ++nestedDirCount;
            }

            assert(!p.native().starts_with("/")); /* Already check that the path is relative, can't happen. */

            /* No symlinks shall be followed. */
            AutoCloseFD nextComponent = ::openat(parentFd, p.c_str(), O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (!nextComponent) {
                if (errno != ENOENT) /* We might have to create it if it doesn't exist. */
                    throw SysError("creating directory component %s in the sandbox path %s", PathFmt(p), PathFmt(path));

                /* Let the umask restrict the permissions. TODO: Do we want
                   this? We don't handle EEXIST here because that would mean
                   somebody is already racing us. Fail closed. */
                if (::mkdirat(parentFd, p.c_str(), 0777) == -1)
                    throw SysError("creating directory component %s in the sandbox path %s", PathFmt(p), PathFmt(path));

                /* Try again after creating the directory. Should succeed. */
                nextComponent = ::openat(parentFd, p.c_str(), O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (!nextComponent) /* Something is terribly wrong, bail out. */
                    throw SysError("creating directory component %s in the sandbox path %s", PathFmt(p), PathFmt(path));
            }

            /* Proceed to the next iteration. */
            maybeParentFdOwned = std::move(nextComponent);
            parentFd = maybeParentFdOwned.get();
        }

        return {std::move(maybeParentFdOwned), parentFd, nestedDirCount == 0};
    };

    AutoCloseFD sourceFd = ::open(source.c_str(), O_PATH | O_NOFOLLOW | O_CLOEXEC);

    /* Skip non-existent files source paths if .optional is set. */
    if (!sourceFd) {
        if (optional && (errno == ENOENT || errno == ENOTDIR))
            return;
        else
            throw SysError("getting attributes of path %1%", PathFmt(source));
    }

    auto st = nix::fstat(sourceFd.get());

    if (S_ISDIR(st.st_mode)) {
        auto [maybeDirFdOwned, dirFd, isRoot] = createDirsAndOpen(target);
        /* We must always open a fresh directory - i.e. the path must not resolve to chrootRootDir itself. */
        if (isRoot || !maybeDirFdOwned)
            throw Error("sandbox path %s escapes the chroot", PathFmt(chrootRootDirPath / target));
        assert(maybeDirFdOwned.get() == dirFd); /* See the comment above. */
        bindMount(sourceFd.get(), dirFd);
    } else if (S_ISLNK(st.st_mode)) {
        /* Symlinks can't be bind-mounted, so copy the contents.
           The kernel implies AT_EMPTY_PATH for readlinkat
           since https://github.com/torvalds/linux/commit/65cfc6722361 (v2.6.39).
           See also: https://github.com/cyphar/libpathrs/issues/18 */
        auto symlinkTarget = readLinkAt(sourceFd.get(), CanonPath::root);
        auto filename = target.filename();
        auto [maybeParentFdOwned, parentFd, isRoot] = createDirsAndOpen(target.parent_path());
        if (::symlinkat(symlinkTarget.data(), parentFd, filename.c_str()) == -1)
            throw SysError("creating symlink %s", PathFmt(chrootRootDirPath / target));
        /* Copy the write/access time from the source symlink. */
        const std::array<::timespec, 2> times = {st.st_atim, st.st_mtim};
        if (::utimensat(parentFd, filename.c_str(), times.data(), AT_SYMLINK_NOFOLLOW) == -1)
            throw SysError("changing write time of %s", PathFmt(chrootRootDirPath / target));
    } else {
        auto [maybeParentFdOwned, parentFd, isRoot] = createDirsAndOpen(target.parent_path());
        /* Strictly speaking, O_NOFOLLOW is redundant because O_CREAT | O_EXCL would never follow links anyway. */
        AutoCloseFD destFd =
            ::openat(parentFd, target.filename().c_str(), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0666);
        if (!destFd)
            throw SysError("creating regular file %s", PathFmt(chrootRootDirPath / target));
        bindMount(sourceFd.get(), destFd.get());
    }
}

static const std::filesystem::path procPath = "/proc";

void LinuxDerivationBuilder::enterChroot()
{
    auto & localSettings = store.config->getLocalSettings();

    /* Set the NO_NEW_PRIVS before doing seccomp/landlock setup.
       landlock_restrict_self requires either NO_NEW_PRIVS or CAP_SYS_ADMIN.
       With user namespaces we do get CAP_SYS_ADMIN. */
    if (!localSettings.allowNewPrivileges)
        if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1)
            throw SysError("failed to set PR_SET_NO_NEW_PRIVS");

    setupSeccomp(localSettings);

#if DO_LANDLOCK
    try {
        setupLandlock();
    } catch (SysError & e) {
        if (e.errNo != EPERM)
            throw;
        /* If allowNewPrivileges is true and we don't have CAP_SYS_ADMIN
           this code path might be hit. */
        warn("setting up landlock: %s", e.message());
    }
#endif

    linux::setPersonality({
        .system = drv.platform,
        .impersonateLinux26 = localSettings.impersonateLinux26,
    });
}

uid_t ChrootLinuxDerivationBuilder::sandboxUid()
{
    return usingUserNamespace ? (!buildUser || buildUser->getUIDCount() == 1 ? 1000 : 0) : buildUser->getUID();
}

gid_t ChrootLinuxDerivationBuilder::sandboxGid()
{
    return usingUserNamespace ? (!buildUser || buildUser->getUIDCount() == 1 ? 100 : 0)
                              : ChrootDerivationBuilder::sandboxGid();
}

std::unique_ptr<UserLock> ChrootLinuxDerivationBuilder::getBuildUser()
{
    return acquireUserLock(settings.nixStateDir, store.config->getLocalSettings(), drv.useUidRange() ? 65536 : 1, true);
}

void ChrootLinuxDerivationBuilder::prepareUser()
{
    if ((buildUser && buildUser->getUIDCount() != 1) || store.config->getLocalSettings().useCgroups) {
        experimentalFeatureSettings.require(Xp::Cgroups);

        /* If we're running from the daemon, then this will return the
           root cgroup of the service. Otherwise, it will return the
           current cgroup. */
        auto cgroupFS = linux::getCgroupFS();
        if (!cgroupFS)
            throw Error("cannot determine the cgroups file system");
        auto rootCgroupPath = *cgroupFS / linux::getRootCgroup().rel();
        if (!pathExists(rootCgroupPath))
            throw Error("expected cgroup directory %s", PathFmt(rootCgroupPath));

        static std::atomic<unsigned int> counter{0};

        cgroup = rootCgroupPath
                 / (buildUser ? fmt("nix-build-uid-%d", buildUser->getUID())
                              : fmt("nix-build-pid-%d-%d", getpid(), counter++));

        debug("using cgroup %s", PathFmt(*cgroup));

        /* When using a build user, record the cgroup we used for that
           user so that if we got interrupted previously, we can kill
           any left-over cgroup first. */
        if (buildUser) {
            auto cgroupsDir = std::filesystem::path{settings.nixStateDir} / "cgroups";
            createDirs(cgroupsDir);

            auto cgroupFile = cgroupsDir / std::to_string(buildUser->getUID());

            if (pathExists(cgroupFile)) {
                auto prevCgroup = readFile(cgroupFile);
                linux::destroyCgroup(prevCgroup);
            }

            writeFile(cgroupFile, cgroup->native());
        }
    }

    // Kill any processes left in the cgroup or build user.
    DerivationBuilderImpl::prepareUser();
}

void ChrootLinuxDerivationBuilder::prepareSandbox()
{
    ChrootDerivationBuilder::prepareSandbox();

    if (cgroup) {
        if (mkdir(cgroup->c_str(), 0755) != 0)
            throw SysError("creating cgroup %s", PathFmt(*cgroup));
        chownToBuilder(*cgroup);
        chownToBuilder(*cgroup / "cgroup.procs");
        chownToBuilder(*cgroup / "cgroup.threads");
        // chownToBuilder(*cgroup / "cgroup.subtree_control");
    }
}

void ChrootLinuxDerivationBuilder::startChild()
{
    RunChildArgs args{
#if NIX_WITH_AWS_AUTH
        .awsCredentials = preResolveAwsCredentials(),
#endif
    };

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

    userNamespaceSync.create();

    usingUserNamespace = userNamespacesSupported();

    Pipe sendPid;
    sendPid.create();

    Pid helper = startProcess([&]() {
        sendPid.readSide.close();

        /* We need to open the slave early, before
           CLONE_NEWUSER. Otherwise we get EPERM when running as
           root. */
        openSlave();

        try {
            /* Drop additional groups here because we can't do it
               after we've created the new user namespace. */
            if (setgroups(0, 0) == -1) {
                if (errno != EPERM)
                    throw SysError("setgroups failed");
                if (store.config->getLocalSettings().requireDropSupplementaryGroups)
                    throw Error(
                        "setgroups failed. Set the require-drop-supplementary-groups option to false to skip this step.");
            }

            ProcessOptions options;
            options.cloneFlags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS | CLONE_PARENT | SIGCHLD;
            if (derivationType.isSandboxed())
                options.cloneFlags |= CLONE_NEWNET;
            if (usingUserNamespace)
                options.cloneFlags |= CLONE_NEWUSER;

            pid_t child = startProcess([this, args = std::move(args)]() { runChild(std::move(args)); }, options);

            writeFull(sendPid.writeSide.get(), fmt("%d\n", child));
            _exit(0);
        } catch (...) {
            handleChildException(true);
            _exit(1);
        }
    });

    sendPid.writeSide.close();

    if (auto status = helper.wait(); !statusOk(status)) {
        processSandboxSetupMessages();
        // Only reached if the child process didn't send an exception.
        throw Error("unable to start build process: %s", statusToString(status));
    }

    userNamespaceSync.readSide = -1;

    /* Make sure that we write *something* to the child in case of
       an exception. Note that merely closing
       `userNamespaceSync.writeSide` doesn't work in
       multi-threaded Nix, since several child processes may have
       inherited `writeSide` (and O_CLOEXEC doesn't help because
       the children may not do an execve). */
    bool userNamespaceSyncDone = false;
    Finally cleanup([&]() {
        try {
            if (!userNamespaceSyncDone)
                writeFull(userNamespaceSync.writeSide.get(), "0\n");
        } catch (...) {
        }
        userNamespaceSync.writeSide = -1;
    });

    FdSource sendPidSource(sendPid.readSide.get());
    auto ss = tokenizeString<std::vector<std::string>>(sendPidSource.readLine());
    assert(ss.size() == 1);
    pid = string2Int<pid_t>(ss[0]).value();
    auto thisProcPath = procPath / std::to_string(static_cast<pid_t>(pid));

    if (usingUserNamespace) {
        /* Set the UID/GID mapping of the builder's user namespace
           such that the sandbox user maps to the build user, or to
           the calling user (if build users are disabled). */
        uid_t hostUid = buildUser ? buildUser->getUID() : getuid();
        uid_t hostGid = buildUser ? buildUser->getGID() : getgid();
        uid_t nrIds = buildUser ? buildUser->getUIDCount() : 1;

        writeFile(thisProcPath / "uid_map", fmt("%d %d %d", sandboxUid(), hostUid, nrIds));

        if (!buildUser || buildUser->getUIDCount() == 1)
            writeFile(thisProcPath / "setgroups", "deny");

        writeFile(thisProcPath / "gid_map", fmt("%d %d %d", sandboxGid(), hostGid, nrIds));
    } else {
        debug("note: not using a user namespace");
        if (!buildUser)
            throw Error(
                "cannot perform a sandboxed build because user namespaces are not enabled; check /proc/sys/user/max_user_namespaces");
    }

    /* Now that we know the sandbox uid/gid, we can write
       /etc/passwd and /etc/group. */
    writeFile(
        chrootRootDir / "etc" / "passwd",
        fmt("root:x:0:0:Nix build user:%3%:/noshell\n"
            "nixbld:x:%1%:%2%:Nix build user:%3%:/noshell\n"
            "nobody:x:65534:65534:Nobody:/:/noshell\n",
            sandboxUid(),
            sandboxGid(),
            store.config->getLocalSettings().sandboxBuildDir.get().native()));

    writeFile(
        chrootRootDir / "etc" / "group",
        fmt("root:x:0:\n"
            "nixbld:!:%1%:\n"
            "nogroup:x:65534:\n",
            sandboxGid()));

    /* Save the mount- and user namespace of the child. We have to do this
     * *before* the child does a chroot. */
    auto sandboxPath = thisProcPath / "ns";
    sandboxMountNamespace = open((sandboxPath / "mnt").c_str(), O_RDONLY | O_CLOEXEC);
    if (sandboxMountNamespace.get() == -1)
        throw SysError("getting sandbox mount namespace");

    if (usingUserNamespace) {
        sandboxUserNamespace = open((sandboxPath / "user").c_str(), O_RDONLY | O_CLOEXEC);
        if (sandboxUserNamespace.get() == -1)
            throw SysError("getting sandbox user namespace");
    }

    /* Move the child into its own cgroup. */
    if (cgroup)
        writeFile(*cgroup / "cgroup.procs", fmt("%d", (pid_t) pid));

    /* Signal the builder that we've updated its user namespace. */
    writeFull(userNamespaceSync.writeSide.get(), "1\n");
    userNamespaceSyncDone = true;
}

void ChrootLinuxDerivationBuilder::enterChroot()
{
    userNamespaceSync.writeSide = -1;

    if (readLine(userNamespaceSync.readSide.get()) != "1")
        throw Error("user namespace initialisation failed");

    userNamespaceSync.readSide = -1;

    if (derivationType.isSandboxed()) {

        /* Initialise the loopback interface. */
        AutoCloseFD fd(socket(PF_INET, SOCK_DGRAM, IPPROTO_IP));
        if (!fd)
            throw SysError("cannot open IP socket");

        using namespace std::string_view_literals;
        struct ifreq ifr = {};
        std::ranges::copy("lo"sv, ifr.ifr_name);
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
        throw SysError("unable to bind mount %1%", PathFmt(chrootRootDir));

    /* Bind-mount the sandbox's Nix store onto itself so that
       we can mark it as a "shared" subtree, allowing bind
       mounts made in *this* mount namespace to be propagated
       into the child namespace created by the
       unshare(CLONE_NEWNS) call below.

       Marking chrootRootDir as MS_SHARED causes pivot_root()
       to fail with EINVAL. Don't know why. */
    std::filesystem::path chrootStoreDir = chrootRootDir / std::filesystem::path(store.storeDir).relative_path();

    if (mount(chrootStoreDir.c_str(), chrootStoreDir.c_str(), 0, MS_BIND, 0) == -1)
        throw SysError("unable to bind mount the Nix store at %1%", PathFmt(chrootStoreDir));

    if (mount(0, chrootStoreDir.c_str(), 0, MS_SHARED, 0) == -1)
        throw SysError("unable to make %s shared", PathFmt(chrootStoreDir));

    /* Set up a nearly empty /dev, unless the user asked to
       bind-mount the host /dev. */
    Strings ss;
    if (pathsInChroot.find("/dev") == pathsInChroot.end()) {
        createDirs(chrootRootDir / "dev" / "shm");
        createDirs(chrootRootDir / "dev" / "pts");
        ss.push_back("/dev/full");
        if (systemFeatures.count("kvm")) {
            if (pathExists("/dev/kvm")) {
                ss.push_back("/dev/kvm");
            } else {
                warn(
                    "KVM is enabled in system-features but /dev/kvm is not available. "
                    "QEMU builds may fall back to slow emulation. "
                    "Consider removing 'kvm' from system-features in nix.conf if KVM is not supported on this system.");
            }
        }
        ss.push_back("/dev/null");
        ss.push_back("/dev/random");
        ss.push_back("/dev/tty");
        ss.push_back("/dev/urandom");
        ss.push_back("/dev/zero");
        createSymlink("/proc/self/fd", chrootRootDir / "dev" / "fd");
        createSymlink("/proc/self/fd/0", chrootRootDir / "dev" / "stdin");
        createSymlink("/proc/self/fd/1", chrootRootDir / "dev" / "stdout");
        createSymlink("/proc/self/fd/2", chrootRootDir / "dev" / "stderr");
    }

    /* Fixed-output derivations typically need to access the
       network, so give them access to /etc/resolv.conf and so
       on. */
    if (!derivationType.isSandboxed()) {
        // Only use nss functions to resolve hosts and
        // services. Don’t use it for anything else that may
        // be configured for this system. This limits the
        // potential impurities introduced in fixed-outputs.
        writeFile(chrootRootDir / "etc" / "nsswitch.conf", "hosts: files dns\nservices: files\n");

        /* N.B. it is realistic that these paths might not exist. It
           happens when testing Nix building fixed-output derivations
           within a pure derivation. */
        for (auto & path : {"/etc/resolv.conf", "/etc/services", "/etc/hosts"})
            if (pathExists(path))
                ss.push_back(path);

        if (auto & caFile = fileTransferSettings.caFile.get()) {
            if (pathExists(*caFile))
                pathsInChroot.try_emplace(
                    "/etc/ssl/certs/ca-certificates.crt", canonPath(caFile->native(), true), true);
        }
    }

    for (auto & i : ss) {
        // For backwards-compatibility, resolve all the symlinks in the
        // chroot paths.
        auto canonicalPath = canonPath(i, true);
        pathsInChroot.emplace(i, canonicalPath);
    }

    /* We really do have to reopen the dirfd in the child, otherwise move_mount dies with EINVAL. */
    AutoCloseFD chrootRootDirFd = openDirectory(chrootRootDir, FinalSymlink::DontFollow);
    if (!chrootRootDirFd)
        throw SysError("opening directory %s", PathFmt(chrootRootDir));

    /* Bind-mount all the directories from the "host"
       filesystem that we want in the chroot
       environment. */
    for (auto & i : pathsInChroot) {
        if (i.second.source == "/proc")
            continue; // backwards compatibility
#if HAVE_EMBEDDED_SANDBOX_SHELL
        if (i.second.source == "__embedded_sandbox_shell__") {
            static constexpr unsigned char sh[] = {
#  embed EMBEDDED_SANDBOX_SHELL_PATH
            };
            auto dst = chrootRootDir / i.first.relative_path();
            createDirs(dst.parent_path());
            writeFile(dst, std::string_view((const char *) sh, sizeof(sh)));
            chmod(dst, 0555);
        } else
#endif
        {
            doBind(i.second.source, chrootRootDirFd.get(), i.first.relative_path(), chrootRootDir, i.second.optional);
        }
    }

    /* Bind a new instance of procfs on /proc. */
    createDirs(chrootRootDir / "proc");
    if (mount("none", (chrootRootDir / "proc").c_str(), "proc", 0, 0) == -1)
        throw SysError("mounting /proc");

    /* Mount sysfs on /sys. */
    if (buildUser && buildUser->getUIDCount() != 1) {
        createDirs(chrootRootDir / "sys");
        if (mount("none", (chrootRootDir / "sys").c_str(), "sysfs", 0, 0) == -1)
            throw SysError("mounting /sys");
    }

    /* Mount a new tmpfs on /dev/shm to ensure that whatever
       the builder puts in /dev/shm is cleaned up automatically. */
    if (pathExists("/dev/shm")
        && mount(
               "none",
               (chrootRootDir / "dev" / "shm").c_str(),
               "tmpfs",
               0,
               fmt("size=%s", store.config->getLocalSettings().sandboxShmSize).c_str())
               == -1)
        throw SysError("mounting /dev/shm");

    /* Mount a new devpts on /dev/pts.  Note that this
       requires the kernel to be compiled with
       CONFIG_DEVPTS_MULTIPLE_INSTANCES=y (which is the case
       if /dev/ptx/ptmx exists). */
    if (pathExists("/dev/pts/ptmx") && !pathExists(chrootRootDir / "dev" / "ptmx")
        && !pathsInChroot.count("/dev/pts")) {
        if (mount("none", (chrootRootDir / "dev" / "pts").c_str(), "devpts", 0, "newinstance,mode=0620") == 0) {
            createSymlink("/dev/pts/ptmx", chrootRootDir / "dev" / "ptmx");

            /* Make sure /dev/pts/ptmx is world-writable.  With some
                Linux versions, it is created with permissions 0.  */
            chmod(chrootRootDir / "dev" / "pts" / "ptmx", 0666);
        } else {
            if (errno != EINVAL)
                throw SysError("mounting /dev/pts");
            doBind("/dev/pts", chrootRootDirFd.get(), "dev/pts", chrootRootDir);
            doBind("/dev/ptmx", chrootRootDirFd.get(), "dev/ptmx", chrootRootDir);
        }
    }

    /* Make /etc unwritable */
    if (!drv.useUidRange())
        chmod(chrootRootDir / "etc", 0555);

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

    /* Unshare the cgroup namespace. This means
       /proc/self/cgroup will show the child's cgroup as '/'
       rather than whatever it is in the parent. */
    if (cgroup && unshare(CLONE_NEWCGROUP) == -1)
        throw SysError("unsharing cgroup namespace");

    /* Do the chroot(). */
    if (chdir(chrootRootDir.c_str()) == -1)
        throw SysError("cannot change directory to %1%", PathFmt(chrootRootDir));

    if (mkdir("real-root", 0500) == -1)
        throw SysError("cannot create real-root directory");

    if (pivot_root(".", "real-root") == -1)
        throw SysError("cannot pivot old root directory onto %1%", PathFmt(chrootRootDir / "real-root"));

    if (chroot(".") == -1)
        throw SysError("cannot change root directory to %1%", PathFmt(chrootRootDir));

    if (umount2("real-root", MNT_DETACH) == -1)
        throw SysError("cannot unmount real root filesystem");

    if (rmdir("real-root") == -1)
        throw SysError("cannot remove real-root directory");

    LinuxDerivationBuilder::enterChroot();
}

void ChrootLinuxDerivationBuilder::setUser()
{
    preserveDeathSignal([this]() {
        /* Switch to the sandbox uid/gid in the user namespace,
           which corresponds to the build user or calling user in
           the parent namespace. */
        if (setgid(sandboxGid()) == -1)
            throw SysError("setgid failed");
        if (setuid(sandboxUid()) == -1)
            throw SysError("setuid failed");
    });
}

SingleDrvOutputs ChrootLinuxDerivationBuilder::unprepareBuild()
{
    sandboxMountNamespace = -1;
    sandboxUserNamespace = -1;

    return DerivationBuilderImpl::unprepareBuild();
}

void ChrootLinuxDerivationBuilder::killSandbox(bool getStats)
{
    if (cgroup) {
        auto stats = linux::destroyCgroup(*cgroup);
        if (getStats) {
            buildResult.cpuUser = stats.cpuUser;
            buildResult.cpuSystem = stats.cpuSystem;
        }
        return;
    }

    DerivationBuilderImpl::killSandbox(getStats);
}

void ChrootLinuxDerivationBuilder::addDependencyImpl(const StorePath & path)
{
    auto [source, targetRelPath] = ChrootDerivationBuilder::addDependencyPrep(path);
    assert(targetRelPath.is_relative()); /* The path is relative to the chroot. */

    /* Bind-mount the path into the sandbox. This requires
       entering its mount namespace, which is not possible
       in multithreaded programs. So we do this in a
       child process.*/
    Pid child(startProcess([&]() {
        if (usingUserNamespace && (setns(sandboxUserNamespace.get(), CLONE_NEWUSER) == -1))
            throw SysError("entering sandbox user namespace");

        if (setns(sandboxMountNamespace.get(), CLONE_NEWNS) == -1)
            throw SysError("entering sandbox mount namespace");

        /* We really do have to reopen the dirfd in the child, otherwise move_mount dies with EINVAL. */
        AutoCloseFD chrootRootDirFd = openDirectory(chrootRootDir, FinalSymlink::DontFollow);
        if (!chrootRootDirFd)
            throw SysError("opening directory %s", PathFmt(chrootRootDir));
        doBind(source, chrootRootDirFd.get(), targetRelPath, chrootRootDir);

        _exit(0);
    }));

    int status = child.wait();
    if (!statusOk(status))
        throw Error("could not add path '%s' to sandbox: %s", store.printStorePath(path), statusToString(status));
}

} // namespace nix
