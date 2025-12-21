#ifdef __linux__

#  include "nix/store/personality.hh"
#  include "nix/util/cgroup.hh"
#  include "nix/util/linux-namespaces.hh"
#  include "nix/util/logging.hh"
#  include "nix/util/serialise.hh"
#  include "linux/fchmodat2-compat.hh"

#  include <sys/ioctl.h>
#  include <net/if.h>
#  include <netinet/ip.h>
#  include <sys/mman.h>
#  include <sched.h>
#  include <sys/param.h>
#  include <sys/mount.h>
#  include <sys/syscall.h>

#  if HAVE_SECCOMP
#    include <seccomp.h>
#  endif

#  define pivot_root(new_root, put_old) (syscall(SYS_pivot_root, new_root, put_old))

namespace nix {

static void setupSeccomp()
{
    if (!settings.filterSyscalls)
        return;

#  if HAVE_SECCOMP
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
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(getxattr), 0) != 0
        || seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(lgetxattr), 0) != 0
        || seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(fgetxattr), 0) != 0
        || seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(setxattr), 0) != 0
        || seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(lsetxattr), 0) != 0
        || seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(fsetxattr), 0) != 0)
        throw SysError("unable to add seccomp rule");

    if (seccomp_attr_set(ctx, SCMP_FLTATR_CTL_NNP, settings.allowNewPrivileges ? 0 : 1) != 0)
        throw SysError("unable to set 'no new privileges' seccomp attribute");

    if (seccomp_load(ctx) != 0)
        throw SysError("unable to load seccomp BPF program");
#  else
    throw Error(
        "seccomp is not supported on this platform; "
        "you can bypass this error by setting the option 'filter-syscalls' to false, but note that untrusted builds can then create setuid binaries!");
#  endif
}

static void doBind(const Path & source, const Path & target, bool optional = false)
{
    debug("bind mounting '%1%' to '%2%'", source, target);

    auto bindMount = [&]() {
        if (mount(source.c_str(), target.c_str(), "", MS_BIND | MS_REC, 0) == -1)
            throw SysError("bind mount from '%1%' to '%2%' failed", source, target);
    };

    auto maybeSt = maybeLstat(source);
    if (!maybeSt) {
        if (optional)
            return;
        else
            throw SysError("getting attributes of path '%1%'", source);
    }
    auto st = *maybeSt;

    if (S_ISDIR(st.st_mode)) {
        createDirs(target);
        bindMount();
    } else if (S_ISLNK(st.st_mode)) {
        // Symlinks can (apparently) not be bind-mounted, so just copy it
        createDirs(dirOf(target));
        copyFile(std::filesystem::path(source), std::filesystem::path(target), false);
    } else {
        createDirs(dirOf(target));
        writeFile(target, "");
        bindMount();
    }
}

struct LinuxDerivationBuilder : virtual DerivationBuilderImpl
{
    using DerivationBuilderImpl::DerivationBuilderImpl;

    void enterChroot() override
    {
        setupSeccomp();

        linux::setPersonality(drv.platform);
    }
};

struct ChrootLinuxDerivationBuilder : ChrootDerivationBuilder, LinuxDerivationBuilder
{
    /**
     * Pipe for synchronising updates to the builder namespaces.
     */
    Pipe userNamespaceSync;

    /**
     * The mount namespace and user namespace of the builder, used to add additional
     * paths to the sandbox as a result of recursive Nix calls.
     */
    AutoCloseFD sandboxMountNamespace;
    AutoCloseFD sandboxUserNamespace;

    /**
     * On Linux, whether we're doing the build in its own user
     * namespace.
     */
    bool usingUserNamespace = true;

    /**
     * The cgroup of the builder, if any.
     */
    std::optional<Path> cgroup;

    ChrootLinuxDerivationBuilder(
        LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
        : DerivationBuilderImpl{store, std::move(miscMethods), std::move(params)}
        , ChrootDerivationBuilder{store, std::move(miscMethods), std::move(params)}
        , LinuxDerivationBuilder{store, std::move(miscMethods), std::move(params)}
    {
    }

    uid_t sandboxUid()
    {
        return usingUserNamespace ? (!buildUser || buildUser->getUIDCount() == 1 ? 1000 : 0) : buildUser->getUID();
    }

    gid_t sandboxGid() override
    {
        return usingUserNamespace ? (!buildUser || buildUser->getUIDCount() == 1 ? 100 : 0)
                                  : ChrootDerivationBuilder::sandboxGid();
    }

    std::unique_ptr<UserLock> getBuildUser() override
    {
        return acquireUserLock(drvOptions.useUidRange(drv) ? 65536 : 1, true);
    }

    void prepareUser() override
    {
        if ((buildUser && buildUser->getUIDCount() != 1) || settings.useCgroups) {
            experimentalFeatureSettings.require(Xp::Cgroups);

            /* If we're running from the daemon, then this will return the
               root cgroup of the service. Otherwise, it will return the
               current cgroup. */
            auto rootCgroup = getRootCgroup();
            auto cgroupFS = getCgroupFS();
            if (!cgroupFS)
                throw Error("cannot determine the cgroups file system");
            auto rootCgroupPath = canonPath(*cgroupFS + "/" + rootCgroup);
            if (!pathExists(rootCgroupPath))
                throw Error("expected cgroup directory '%s'", rootCgroupPath);

            static std::atomic<unsigned int> counter{0};

            cgroup = buildUser ? fmt("%s/nix-build-uid-%d", rootCgroupPath, buildUser->getUID())
                               : fmt("%s/nix-build-pid-%d-%d", rootCgroupPath, getpid(), counter++);

            debug("using cgroup '%s'", *cgroup);

            /* When using a build user, record the cgroup we used for that
               user so that if we got interrupted previously, we can kill
               any left-over cgroup first. */
            if (buildUser) {
                auto cgroupsDir = settings.nixStateDir + "/cgroups";
                createDirs(cgroupsDir);

                auto cgroupFile = fmt("%s/%d", cgroupsDir, buildUser->getUID());

                if (pathExists(cgroupFile)) {
                    auto prevCgroup = readFile(cgroupFile);
                    destroyCgroup(prevCgroup);
                }

                writeFile(cgroupFile, *cgroup);
            }
        }

        // Kill any processes left in the cgroup or build user.
        DerivationBuilderImpl::prepareUser();
    }

    void prepareSandbox() override
    {
        ChrootDerivationBuilder::prepareSandbox();

        if (cgroup) {
            if (mkdir(cgroup->c_str(), 0755) != 0)
                throw SysError("creating cgroup '%s'", *cgroup);
            chownToBuilder(*cgroup);
            chownToBuilder(*cgroup + "/cgroup.procs");
            chownToBuilder(*cgroup + "/cgroup.threads");
            // chownToBuilder(*cgroup + "/cgroup.subtree_control");
        }
    }

    void startChild() override
    {
        RunChildArgs args{
#  if NIX_WITH_AWS_AUTH
            .awsCredentials = preResolveAwsCredentials(),
#  endif
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
                    if (settings.requireDropSupplementaryGroups)
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

        if (helper.wait() != 0) {
            processSandboxSetupMessages();
            // Only reached if the child process didn't send an exception.
            throw Error("unable to start build process");
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

        if (usingUserNamespace) {
            /* Set the UID/GID mapping of the builder's user namespace
               such that the sandbox user maps to the build user, or to
               the calling user (if build users are disabled). */
            uid_t hostUid = buildUser ? buildUser->getUID() : getuid();
            uid_t hostGid = buildUser ? buildUser->getGID() : getgid();
            uid_t nrIds = buildUser ? buildUser->getUIDCount() : 1;

            writeFile("/proc/" + std::to_string(pid) + "/uid_map", fmt("%d %d %d", sandboxUid(), hostUid, nrIds));

            if (!buildUser || buildUser->getUIDCount() == 1)
                writeFile("/proc/" + std::to_string(pid) + "/setgroups", "deny");

            writeFile("/proc/" + std::to_string(pid) + "/gid_map", fmt("%d %d %d", sandboxGid(), hostGid, nrIds));
        } else {
            debug("note: not using a user namespace");
            if (!buildUser)
                throw Error(
                    "cannot perform a sandboxed build because user namespaces are not enabled; check /proc/sys/user/max_user_namespaces");
        }

        /* Now that we now the sandbox uid, we can write
           /etc/passwd. */
        writeFile(
            chrootRootDir + "/etc/passwd",
            fmt("root:x:0:0:Nix build user:%3%:/noshell\n"
                "nixbld:x:%1%:%2%:Nix build user:%3%:/noshell\n"
                "nobody:x:65534:65534:Nobody:/:/noshell\n",
                sandboxUid(),
                sandboxGid(),
                settings.sandboxBuildDir));

        /* Save the mount- and user namespace of the child. We have to do this
         *before* the child does a chroot. */
        sandboxMountNamespace = open(fmt("/proc/%d/ns/mnt", (pid_t) pid).c_str(), O_RDONLY);
        if (sandboxMountNamespace.get() == -1)
            throw SysError("getting sandbox mount namespace");

        if (usingUserNamespace) {
            sandboxUserNamespace = open(fmt("/proc/%d/ns/user", (pid_t) pid).c_str(), O_RDONLY);
            if (sandboxUserNamespace.get() == -1)
                throw SysError("getting sandbox user namespace");
        }

        /* Move the child into its own cgroup. */
        if (cgroup)
            writeFile(*cgroup + "/cgroup.procs", fmt("%d", (pid_t) pid));

        /* Signal the builder that we've updated its user namespace. */
        writeFull(userNamespaceSync.writeSide.get(), "1\n");
        userNamespaceSyncDone = true;
    }

    void enterChroot() override
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
        Path chrootStoreDir = chrootRootDir + store.storeDir;

        if (mount(chrootStoreDir.c_str(), chrootStoreDir.c_str(), 0, MS_BIND, 0) == -1)
            throw SysError("unable to bind mount the Nix store", chrootStoreDir);

        if (mount(0, chrootStoreDir.c_str(), 0, MS_SHARED, 0) == -1)
            throw SysError("unable to make '%s' shared", chrootStoreDir);

        /* Set up a nearly empty /dev, unless the user asked to
           bind-mount the host /dev. */
        Strings ss;
        if (pathsInChroot.find("/dev") == pathsInChroot.end()) {
            createDirs(chrootRootDir + "/dev/shm");
            createDirs(chrootRootDir + "/dev/pts");
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
            createSymlink("/proc/self/fd", chrootRootDir + "/dev/fd");
            createSymlink("/proc/self/fd/0", chrootRootDir + "/dev/stdin");
            createSymlink("/proc/self/fd/1", chrootRootDir + "/dev/stdout");
            createSymlink("/proc/self/fd/2", chrootRootDir + "/dev/stderr");
        }

        /* Fixed-output derivations typically need to access the
           network, so give them access to /etc/resolv.conf and so
           on. */
        if (!derivationType.isSandboxed()) {
            // Only use nss functions to resolve hosts and
            // services. Don’t use it for anything else that may
            // be configured for this system. This limits the
            // potential impurities introduced in fixed-outputs.
            writeFile(chrootRootDir + "/etc/nsswitch.conf", "hosts: files dns\nservices: files\n");

            /* N.B. it is realistic that these paths might not exist. It
               happens when testing Nix building fixed-output derivations
               within a pure derivation. */
            for (auto & path : {"/etc/resolv.conf", "/etc/services", "/etc/hosts"})
                if (pathExists(path))
                    ss.push_back(path);

            if (settings.caFile != "") {
                Path caFile = settings.caFile;
                if (pathExists(caFile))
                    pathsInChroot.try_emplace("/etc/ssl/certs/ca-certificates.crt", canonPath(caFile, true), true);
            }
        }

        for (auto & i : ss) {
            // For backwards-compatibility, resolve all the symlinks in the
            // chroot paths.
            auto canonicalPath = canonPath(i, true);
            pathsInChroot.emplace(i, canonicalPath);
        }

        /* Bind-mount all the directories from the "host"
           filesystem that we want in the chroot
           environment. */
        for (auto & i : pathsInChroot) {
            if (i.second.source == "/proc")
                continue; // backwards compatibility

#  if HAVE_EMBEDDED_SANDBOX_SHELL
            if (i.second.source == "__embedded_sandbox_shell__") {
                static unsigned char sh[] = {
#    include "embedded-sandbox-shell.gen.hh"
                };
                auto dst = chrootRootDir + i.first;
                createDirs(dirOf(dst));
                writeFile(dst, std::string_view((const char *) sh, sizeof(sh)));
                chmod_(dst, 0555);
            } else
#  endif
            {
                doBind(i.second.source, chrootRootDir + i.first, i.second.optional);
            }
        }

        /* Bind a new instance of procfs on /proc. */
        createDirs(chrootRootDir + "/proc");
        if (mount("none", (chrootRootDir + "/proc").c_str(), "proc", 0, 0) == -1)
            throw SysError("mounting /proc");

        /* Mount sysfs on /sys. */
        if (buildUser && buildUser->getUIDCount() != 1) {
            createDirs(chrootRootDir + "/sys");
            if (mount("none", (chrootRootDir + "/sys").c_str(), "sysfs", 0, 0) == -1)
                throw SysError("mounting /sys");
        }

        /* Mount a new tmpfs on /dev/shm to ensure that whatever
           the builder puts in /dev/shm is cleaned up automatically. */
        if (pathExists("/dev/shm")
            && mount(
                   "none",
                   (chrootRootDir + "/dev/shm").c_str(),
                   "tmpfs",
                   0,
                   fmt("size=%s", settings.sandboxShmSize).c_str())
                   == -1)
            throw SysError("mounting /dev/shm");

        /* Mount a new devpts on /dev/pts.  Note that this
           requires the kernel to be compiled with
           CONFIG_DEVPTS_MULTIPLE_INSTANCES=y (which is the case
           if /dev/ptx/ptmx exists). */
        if (pathExists("/dev/pts/ptmx") && !pathExists(chrootRootDir + "/dev/ptmx")
            && !pathsInChroot.count("/dev/pts")) {
            if (mount("none", (chrootRootDir + "/dev/pts").c_str(), "devpts", 0, "newinstance,mode=0620") == 0) {
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

        /* Make /etc unwritable */
        if (!drvOptions.useUidRange(drv))
            chmod_(chrootRootDir + "/etc", 0555);

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
            throw SysError("cannot change directory to '%1%'", chrootRootDir);

        if (mkdir("real-root", 0500) == -1)
            throw SysError("cannot create real-root directory");

        if (pivot_root(".", "real-root") == -1)
            throw SysError("cannot pivot old root directory onto '%1%'", (chrootRootDir + "/real-root"));

        if (chroot(".") == -1)
            throw SysError("cannot change root directory to '%1%'", chrootRootDir);

        if (umount2("real-root", MNT_DETACH) == -1)
            throw SysError("cannot unmount real root filesystem");

        if (rmdir("real-root") == -1)
            throw SysError("cannot remove real-root directory");

        LinuxDerivationBuilder::enterChroot();
    }

    void setUser() override
    {
        /* Switch to the sandbox uid/gid in the user namespace,
           which corresponds to the build user or calling user in
           the parent namespace. */
        if (setgid(sandboxGid()) == -1)
            throw SysError("setgid failed");
        if (setuid(sandboxUid()) == -1)
            throw SysError("setuid failed");
    }

    SingleDrvOutputs unprepareBuild() override
    {
        sandboxMountNamespace = -1;
        sandboxUserNamespace = -1;

        return DerivationBuilderImpl::unprepareBuild();
    }

    void killSandbox(bool getStats) override
    {
        if (cgroup) {
            auto stats = destroyCgroup(*cgroup);
            if (getStats) {
                buildResult.cpuUser = stats.cpuUser;
                buildResult.cpuSystem = stats.cpuSystem;
            }
            return;
        }

        DerivationBuilderImpl::killSandbox(getStats);
    }

    void addDependencyImpl(const StorePath & path) override
    {
        auto [source, target] = ChrootDerivationBuilder::addDependencyPrep(path);

        /* Bind-mount the path into the sandbox. This requires
           entering its mount namespace, which is not possible
           in multithreaded programs. So we do this in a
           child process.*/
        Pid child(startProcess([&]() {
            if (usingUserNamespace && (setns(sandboxUserNamespace.get(), 0) == -1))
                throw SysError("entering sandbox user namespace");

            if (setns(sandboxMountNamespace.get(), 0) == -1)
                throw SysError("entering sandbox mount namespace");

            doBind(source, target);

            _exit(0);
        }));

        int status = child.wait();
        if (status != 0)
            throw Error("could not add path '%s' to sandbox", store.printStorePath(path));
    }
};

} // namespace nix

#endif
