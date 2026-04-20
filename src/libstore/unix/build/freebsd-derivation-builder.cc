#ifdef __FreeBSD__

#  include "freebsd-derivation-builder.hh"
#  include "generic-unix-derivation-builder.hh"
#  include "derivation-builder-common.hh"
#  include "chroot.hh"
#  include "nix/store/build/derivation-builder.hh"
#  include "nix/util/file-system.hh"
#  include "nix/store/local-store.hh"
#  include "nix/util/processes.hh"
#  include "nix/store/builtins.hh"
#  include "nix/store/build/child.hh"
#  include "nix/store/restricted-store.hh"
#  include "nix/store/user-lock.hh"
#  include "nix/store/globals.hh"
#  include "nix/store/filetransfer.hh"
#  include "store-config-private.hh"
#  include "nix/util/freebsd-jail.hh"

#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/resource.h>
#  include <stdlib.h>
#  include <string.h>

#  include <db.h>
#  include <pwd.h>
#  include <sys/mount.h>
#  include <netlink/netlink_snl.h>
#  include <netlink/netlink_snl_route.h>
#  include <net/if.h>
#  include <sys/param.h>
#  include <sys/jail.h>
#  include <sys/sockio.h>
#  include <jail.h>

#  if NIX_WITH_AWS_AUTH
#    include "nix/store/aws-creds.hh"
#  endif

namespace nix {

namespace {

struct PasswordEntry
{
    std::string name;
    uid_t uid;
    gid_t gid;
    std::string description;
    std::filesystem::path home;
    std::filesystem::path shell;
};

using UniqueDB = std::unique_ptr<::DB, decltype([](::DB * db) {
                                     if (db)
                                         (db->close)(db);
                                 })>;

// Database open flags from FreeBSD, in case they're necessary for compatibility
static constexpr HASHINFO dbFlags = {
    .bsize = 4096,
    .ffactor = 32,
    .nelem = 256,
    .cachesize = 2 * 1024 * 1024,
    .hash = nullptr,
    .lorder = BIG_ENDIAN,
};

// Password database version
// Version 4 has been current since 2003
static const uint8_t dbVersion = 4;

static void serializeString(std::vector<uint8_t> & buf, std::string const & str)
{
    buf.insert(buf.end(), str.begin(), str.end());
    buf.push_back(0);
}

static void serializeInt(std::vector<uint8_t> & buf, uint32_t num)
{
    // Always big endian
    buf.push_back((num >> 24) & 0xff);
    buf.push_back((num >> 16) & 0xff);
    buf.push_back((num >> 8) & 0xff);
    buf.push_back((num >> 0) & 0xff);
}

static std::vector<uint8_t> byNameKey(std::string const & name)
{
    std::vector<uint8_t> buf{_PW_VERSIONED(_PW_KEYBYNAME, dbVersion)};
    buf.reserve(1 + name.size());
    // We can't use serializeString since that's null terminated
    buf.insert(buf.end(), name.begin(), name.end());

    return buf;
}

static std::vector<uint8_t> byNumKey(uint32_t num)
{
    std::vector<uint8_t> buf{_PW_VERSIONED(_PW_KEYBYNUM, dbVersion)};
    serializeInt(buf, num);

    return buf;
}

static std::vector<uint8_t> byUidKey(uid_t uid)
{
    std::vector<uint8_t> buf{_PW_VERSIONED(_PW_KEYBYUID, dbVersion)};
    serializeInt(buf, uid);

    return buf;
}

static void createPasswordFiles(std::filesystem::path & chrootRootDir, std::vector<PasswordEntry> & users)
{
    auto db =
        UniqueDB(::dbopen((chrootRootDir / "etc/pwd.db").c_str(), O_CREAT | O_RDWR | O_EXCL, 0644, DB_HASH, &dbFlags));

    if (!db)
        throw SysError("could not create password database");

    auto dbInsert = [&db](std::vector<uint8_t> keyBuf, std::vector<uint8_t> & valueBuf) {
        DBT key = {keyBuf.data(), keyBuf.size()};
        DBT value = {valueBuf.data(), valueBuf.size()};

        if ((db->put)(db.get(), &key, &value, R_NOOVERWRITE) == -1) {
            throw SysError("could not write to password database");
        }
    };

    // Annoyingly DBT doesn't have const pointers so we need this whole shuffle
    std::string versionKeyStr(_PWD_VERSION_KEY);
    std::vector<uint8_t> versionKey(versionKeyStr.begin(), versionKeyStr.end());
    std::vector<uint8_t> versionValue{dbVersion};
    dbInsert(versionKey, versionValue);

    for (const auto & [i, user] : enumerate(users)) {
        // flags for non-empty fields
        uint32_t fields = _PWF_NAME | _PWF_PASSWD | _PWF_UID | _PWF_GID | _PWF_GECOS | _PWF_DIR | _PWF_SHELL;

        std::vector<uint8_t> buf;
        serializeString(buf, user.name);
        // pw_password is always "*" in the insecure database
        serializeString(buf, std::string("*"));
        serializeInt(buf, user.uid);
        serializeInt(buf, user.gid);
        // pw_change = 0 means no requirement to change password
        serializeInt(buf, 0);
        // pw_class is empty since we don't make a class database
        serializeString(buf, std::string(""));
        serializeString(buf, user.description);
        serializeString(buf, user.home);
        serializeString(buf, user.shell);
        // pw_expire = 0 means password does not expire
        serializeInt(buf, 0);
        serializeInt(buf, fields);

        dbInsert(byNameKey(user.name), buf);
        // _PW_KEYBYNUM is 1-indexed
        dbInsert(byNumKey(i + 1), buf);
        dbInsert(byUidKey(user.uid), buf);
    }

    // FreeBSD libc doesn't use /etc/passwd, but some software might
    std::string passwdContent;
    for (const auto & user : users) {
        passwdContent.append(
            fmt("%s:*:%d:%d:%s:%s:%s\n",
                user.name,
                user.uid,
                user.gid,
                user.description,
                user.home.native(),
                user.shell.native()));
    }

    writeFile(chrootRootDir / "etc/passwd", passwdContent);

    // No need to make /etc/master.passwd or /etc/spwd.db,
    // our build user wouldn't be able to read them anyway
}

template<size_t N>
struct iovec iovFromMutableBuffer(std::array<char, N> & array)
{
    return {
        .iov_base = static_cast<void *>(array.data()),
        .iov_len = N,
    };
}

template<size_t N>
struct iovec iovFromStaticSizedString(const char (&array)[N])
{
    return {
        .iov_base = const_cast<void *>(static_cast<const void *>(array)),
        .iov_len = N,
    };
}

struct iovec iovFromDynamicSizeString(const std::string & s)
{
    return {
        .iov_base = const_cast<void *>(static_cast<const void *>(s.c_str())),
        .iov_len = s.length() + 1,
    };
}

} // namespace

struct FreeBSDChrootDerivationBuilder : DerivationBuilder, DerivationBuilderParams, BuilderCore
{
    std::filesystem::path chrootRootDir;

    std::optional<AutoDelete> autoDelChroot;

    std::shared_ptr<AutoRemoveJail> autoDelJail = std::make_shared<AutoRemoveJail>();

    FreeBSDChrootDerivationBuilder(
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

    gid_t sandboxGid()
    {
        return buildUser->getGID();
    }

    std::filesystem::path tmpDirInSandbox()
    {
        return store.config->getLocalSettings().sandboxBuildDir.get();
    }

    void addDependencyImpl(const StorePath & path) override
    {
        throw UnimplementedError(
            "adding store path '%s' to the sandbox is not implemented (recursive-nix)", store.printStorePath(path));
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

        autoDelJail->remove();

        if (autoDelChroot) {
            /* Move paths out of the chroot for easier debugging of
               build failures. */
            if (!force && buildMode == bmNormal)
                for (auto & [_, status] : initialOutputs) {
                    if (!status.known)
                        continue;
                    if (buildMode != bmCheck && status.known->isValid())
                        continue;
                    std::filesystem::path p = store.toRealPath(status.known->path);
                    std::filesystem::path chrootPath = chrootRootDir / p.relative_path();
                    if (pathExists(chrootPath))
                        std::filesystem::rename(chrootPath, p);
                }

            autoDelChroot.reset();
        }
    }

    void prepareSandbox(PathsInChroot & pathsInChroot)
    {
        std::vector<PasswordEntry> users{
            {
                .name = "root",
                .uid = 0,
                .gid = 0,
                .description = "Nix build user",
                .home = store.config->getLocalSettings().sandboxBuildDir,
                .shell = "/noshell",
            },
            {
                .name = "nixbld",
                .uid = buildUser->getUID(),
                .gid = sandboxGid(),
                .description = "Nix build user",
                .home = store.config->getLocalSettings().sandboxBuildDir,
                .shell = "/noshell",
            },
            {
                .name = "nobody",
                .uid = 65534,
                .gid = 65534,
                .description = "Nobody",
                .home = "/",
                .shell = "/noshell",
            },
        };

        createPasswordFiles(chrootRootDir, users);

        // FreeBSD doesn't have a group database, just write a text file
        writeFile(
            chrootRootDir / "etc/group",
            fmt("root:x:0:\n"
                "nixbld:!:%1%:\n"
                "nogroup:x:65534:\n",
                sandboxGid()));

        // Linux waits until after entering the child to start mounting so it doesn't
        // pollute the root mount namespace.
        // FreeBSD doesn't have mount namespaces, so there's no reason to wait.

        auto devpath = chrootRootDir / "dev";
        createDir(devpath, 0555);
        createDir(chrootRootDir / "bin", 0555);

        std::array<char, 255> errmsg{};
        std::array<::iovec, 8> iov = {
            iovFromStaticSizedString("fstype"),
            iovFromStaticSizedString("devfs"),
            iovFromStaticSizedString("fspath"),
            iovFromDynamicSizeString(devpath.native()),
            iovFromStaticSizedString("ruleset"),
            iovFromStaticSizedString("4"),
            iovFromStaticSizedString("errmsg"),
            iovFromMutableBuffer(errmsg),
        };

        if (nmount(iov.data(), iov.size(), 0) < 0)
            throw SysError("failed to mount jail /dev: %1%", std::string_view(errmsg.data()));

        autoDelJail->childrenMounts.emplace_back(devpath);

        for (const auto & [target, chrootPath] : pathsInChroot) {
            std::filesystem::path path = chrootRootDir / target.relative_path();

            auto maybeSt = maybeLstat(chrootPath.source);
            if (!maybeSt) {
                if (chrootPath.optional)
                    continue; /* Skip mounting this path. */
                else
                    throw SysError("getting attributes of path %1%", PathFmt(chrootPath.source));
            }

            /* Mount points must exist and be the right type. */
            if (S_ISDIR(maybeSt->st_mode)) {
                createDirs(path);
            } else if (S_ISLNK(maybeSt->st_mode)) {
                createDirs(path.parent_path());
                copyFile(chrootPath.source, path, /*andDelete=*/false, /*contents=*/false);
                continue;
            } else {
                createDirs(path.parent_path());
                writeFile(path, "");
            }

            std::array<::iovec, 8> nullfsIov = {
                iovFromStaticSizedString("fstype"),
                iovFromStaticSizedString("nullfs"),
                iovFromStaticSizedString("fspath"),
                iovFromDynamicSizeString(path.native()),
                iovFromStaticSizedString("target"),
                iovFromDynamicSizeString(chrootPath.source.native()),
                iovFromStaticSizedString("errmsg"),
                iovFromMutableBuffer(errmsg),
            };

            debug("setting up a nullfs mount from %1% to %2%", PathFmt(chrootPath.source), PathFmt(path));

            int flags = 0;
            if (store.isInStore(target.native()))
                /* While we are at it, enforce invariants about store paths. Anything located at the "logical" store
                   location must be readonly (file permission canonicalisation enforces this on the host filesystem).
                   Also the store must never contain setuid binaries for the same reason. This is just defense-in-depth.
                 */
                flags = MNT_RDONLY | MNT_NOSUID;

            if (nmount(nullfsIov.data(), nullfsIov.size(), flags) < 0)
                throw SysError("failed to mount nullfs for %1%: %2%", PathFmt(path), std::string_view(errmsg.data()));

            autoDelJail->childrenMounts.emplace_back(path);
        }

        /* Fixed-output derivations typically need to access the
           network, so give them access to /etc/resolv.conf and so
           on. */
        if (!derivationType.isSandboxed()) {
            // Only use nss functions to resolve hosts and
            // services. Don't use it for anything else that may
            // be configured for this system. This limits the
            // potential impurities introduced in fixed-outputs.
            writeFile(chrootRootDir / "etc/nsswitch.conf", "hosts: files dns\nservices: files\n");

            /* N.B. it is realistic that these paths might not exist. It
               happens when testing Nix building fixed-output derivations
               within a pure derivation. */
            for (std::filesystem::path path : {"/etc/resolv.conf", "/etc/services", "/etc/hosts"}) {
                if (pathExists(path)) {
                    copyFile(path, chrootRootDir / path.relative_path(), false, true);
                }
            }

            if (fileTransferSettings.caFile.get() && pathExists(fileTransferSettings.caFile.get().value())) {
                createDirs(chrootRootDir / "etc/ssl/certs");
                copyFile(
                    fileTransferSettings.caFile.get().value(),
                    chrootRootDir / "etc/ssl/certs/ca-certificates.crt",
                    false,
                    true);
            }
        }
    }

    std::optional<Descriptor> startBuild() override
    {
        if (useBuildUsers(localSettings)) {
            if (!buildUser)
                buildUser = acquireUserLock(settings.nixStateDir, localSettings, 1, true);

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

        AutoCloseFD tmpDirFd{open(tmpDir.c_str(), O_RDONLY | O_NOFOLLOW | O_DIRECTORY)};
        if (!tmpDirFd)
            throw SysError("failed to open the build temporary directory descriptor %1%", PathFmt(tmpDir));

        nix::chownToBuilder(buildUser.get(), tmpDirFd.get(), tmpDir);

        StringMap inputRewrites;
        std::tie(scratchOutputs, inputRewrites, redirectedOutputs) =
            nix::computeScratchOutputs(store, *this, /* needsHashRewrite= */ false);

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
            throw Error("feature 'uid-range' is not supported on FreeBSD");

        // Start with the default sandbox paths
        PathsInChroot pathsInChroot;
        pathsInChroot = defaultPathsInChroot;

        if (hasPrefix(store.storeDir, tmpDirInSandbox().native())) {
            throw Error("`sandbox-build-dir` must not contain the storeDir");
        }
        pathsInChroot[tmpDirInSandbox()] = {.source = tmpDir};

        nix::checkAndAddImpurePaths(pathsInChroot, drvOptions, store, drvPath, localSettings.allowedImpureHostPrefixes);

        for (auto & i : inputPaths) {
            auto p = store.printStorePath(i);
            pathsInChroot.insert_or_assign(p, ChrootPath{.source = store.toRealPath(i)});
        }

        for (auto & i : drv.outputsAndOptPaths(store)) {
            if (i.second.second)
                pathsInChroot.erase(store.printStorePath(*i.second.second));
        }

        // Set up chroot parameters
        BuildChrootParams chrootParams{
            .chrootParentDir = store.toRealPath(drvPath) + ".chroot",
            .useUidRange = false,
            .isSandboxed = derivationType.isSandboxed(),
            .buildUser = buildUser.get(),
            .storeDir = store.storeDir,
            .chownToBuilder =
                [this](const std::filesystem::path & path) { nix::chownToBuilder(buildUser.get(), path); },
            .getSandboxGid = [this]() { return this->sandboxGid(); },
        };

        auto [rootDir, cleanup] = setupBuildChroot(chrootParams);
        chrootRootDir = std::move(rootDir);
        autoDelChroot.emplace(std::move(cleanup));

        // Set up FreeBSD-specific sandbox content (password DB, devfs, nullfs mounts)
        prepareSandbox(pathsInChroot);

        if (localSettings.preBuildHook != "") {
            printMsg(lvlChatty, "executing pre-build hook '%1%'", localSettings.preBuildHook);
            assert(!chrootRootDir.empty());
            auto lines = runProgram(
                localSettings.preBuildHook.get(),
                false,
                Strings({store.printStorePath(drvPath), chrootRootDir.native()}));
            nix::parsePreBuildHook(pathsInChroot, lines);
        }

        if (drvOptions.getRequiredSystemFeatures(drv).count("recursive-nix"))
            daemon.start(store, *this, env, tmpDir, tmpDirInSandbox(), buildUser.get());

        nix::logBuilderInfo(drv);

        miscMethods->openLogFile();

        nix::setupPTYMaster(builderOut, buildUser.get());

        buildResult.startTime = time(0);

        // Create jail and start child
        {
#  if NIX_WITH_AWS_AUTH
            auto awsCredentials = nix::preResolveAwsCredentials(drv);
#  endif

            int jid;

            if (derivationType.isSandboxed()) {
                jid = jail_setv(
                    JAIL_CREATE,
                    "persist",
                    "true",
                    "path",
                    chrootRootDir.c_str(),
                    "host.hostname",
                    "localhost",
                    // TODO: Make our own ruleset
                    "vnet",
                    "new",
                    nullptr);
                if (jid < 0) {
                    throw SysError("failed to create jail (isolated network): %1%", jail_errmsg);
                }
                autoDelJail->jid = jid;

                // Everything from here to the end of the block is setting up the network
                // code adapted from freebsd/sbin/ifconfig/af_inet.c, in_exec_nl
                Pid helper = startProcess([&]() {
                    unix::closeExtraFDs();
                    enterChroot();

                    struct snl_state ss = {};
                    if (!snl_init(&ss, NETLINK_ROUTE)) {
                        throw SysError("Failed to init netlink connection");
                    }

                    struct snl_writer nw = {};
                    snl_init_writer(&ss, &nw);
                    struct nlmsghdr * hdr = snl_create_msg_request(&nw, NL_RTM_NEWADDR);
                    struct ifaddrmsg * ifahdr = snl_reserve_msg_object(&nw, struct ifaddrmsg);

                    ifahdr->ifa_family = AF_INET;
                    ifahdr->ifa_prefixlen = 8;
                    ifahdr->ifa_index = if_nametoindex("lo0");
                    snl_add_msg_attr_ip4(&nw, IFA_LOCAL, (const struct in_addr *) "\x7f\x00\x00\x01");

                    int off = snl_add_msg_attr_nested(&nw, IFA_FREEBSD);
                    snl_add_msg_attr_u32(&nw, IFAF_FLAGS, IFF_LOOPBACK | IFF_UP);
                    snl_end_attr_nested(&nw, off);

                    if (!(hdr = snl_finalize_msg(&nw)) || !snl_send_message(&ss, hdr)) {
                        snl_free(&ss);
                        throw SysError("failed to sendoff netlink message");
                    }

                    struct snl_errmsg_data e = {};
                    snl_read_reply_code(&ss, hdr->nlmsg_seq, &e);
                    if (e.error_str != nullptr) {
                        snl_free(&ss);
                        throw SysError("failed to configure loopback interface: %1%", e.error_str);
                    }
                    snl_free(&ss);
                    _exit(0);
                });

                /* TODO: Capture the error from the helper? */
                if (auto status = helper.wait(); !statusOk(status)) {
                    throw Error("failed to configure loopback address: %s", statusToString(status));
                }
            } else {
                jid = jail_setv(
                    JAIL_CREATE,
                    "persist",
                    "true",
                    // 4 is the most restrictive devfs ruleset that meets our needs
                    // which is found in the default installation. Trying to add
                    // another one is a huge pain...
                    "devfs_ruleset",
                    "4",
                    "path",
                    chrootRootDir.c_str(),
                    "host.hostname",
                    "localhost",
                    "ip4",
                    "inherit",
                    "ip6",
                    "inherit",
                    "allow.raw_sockets",
                    "true",
                    nullptr);
                if (jid < 0) {
                    throw SysError("failed to create jail (networked): %1%", jail_errmsg);
                }
                autoDelJail->jid = jid;
            }

            pid = startProcess([&]() {
                openSlave();
                runChild(args);
            });
        }

        void enterChroot() override
        {
            /* Close all other file descriptors. This must happen before
               jail_attach for FreeBSD. */
            unix::closeExtraFDs();

            if (jail_attach(autoDelJail->jid) < 0) {
                throw SysError("failed to attach to jail");
            }
        }

        void addDependency(const StorePath & path)
        {
            throw UnimplementedError(
                "adding store path '%s' to the sandbox is not implemented (recursive-nix)", store.printStorePath(path));
            auto [rootDir, cleanup] = setupBuildChroot(chrootParams);
            chrootRootDir = std::move(rootDir);
            autoDelChroot.emplace(std::move(cleanup));

            // Set up FreeBSD-specific sandbox content (password DB, devfs, nullfs mounts)
            prepareSandbox(pathsInChroot);

            if (localSettings.preBuildHook != "") {
                printMsg(lvlChatty, "executing pre-build hook '%1%'", localSettings.preBuildHook);
                assert(!chrootRootDir.empty());
                auto lines = runProgram(
                    localSettings.preBuildHook.get(),
                    false,
                    Strings({store.printStorePath(drvPath), chrootRootDir.native()}));
                nix::parsePreBuildHook(pathsInChroot, lines);
            }

            if (drvOptions.getRequiredSystemFeatures(drv).count("recursive-nix"))
                daemon.start(store, *this, env, tmpDir, tmpDirInSandbox(), buildUser.get());

            nix::logBuilderInfo(drv);

            miscMethods->openLogFile();

            nix::setupPTYMaster(builderOut, buildUser.get());

            buildResult.startTime = time(0);

            // Create jail and start child
            {
#  if NIX_WITH_AWS_AUTH
                auto awsCredentials = nix::preResolveAwsCredentials(drv);
#  endif

                int jid;

                if (derivationType.isSandboxed()) {
                    jid = jail_setv(
                        JAIL_CREATE,
                        "persist",
                        "true",
                        "path",
                        chrootRootDir.c_str(),
                        "host.hostname",
                        "localhost",
                        // TODO: Make our own ruleset
                        "vnet",
                        "new",
                        nullptr);
                    if (jid < 0) {
                        throw SysError("failed to create jail (isolated network): %1%", jail_errmsg);
                    }
                    autoDelJail->jid = jid;

                    // Set up loopback interface inside the jail's vnet
                    // Code adapted from freebsd/sbin/ifconfig/af_inet.c, in_exec_nl
                    Pid helper = startProcess([&]() {
                        unix::closeExtraFDs();

                        if (jail_attach(autoDelJail->jid) < 0) {
                            throw SysError("failed to attach to jail");
                        }

                        struct snl_state ss = {};
                        if (!snl_init(&ss, NETLINK_ROUTE)) {
                            throw SysError("Failed to init netlink connection");
                        }

                        struct snl_writer nw = {};
                        snl_init_writer(&ss, &nw);
                        struct nlmsghdr * hdr = snl_create_msg_request(&nw, NL_RTM_NEWADDR);
                        struct ifaddrmsg * ifahdr = snl_reserve_msg_object(&nw, struct ifaddrmsg);

                        ifahdr->ifa_family = AF_INET;
                        ifahdr->ifa_prefixlen = 8;
                        ifahdr->ifa_index = if_nametoindex("lo0");
                        snl_add_msg_attr_ip4(&nw, IFA_LOCAL, (const struct in_addr *) "\x7f\x00\x00\x01");

                        int off = snl_add_msg_attr_nested(&nw, IFA_FREEBSD);
                        snl_add_msg_attr_u32(&nw, IFAF_FLAGS, IFF_LOOPBACK | IFF_UP);
                        snl_end_attr_nested(&nw, off);

                        if (!(hdr = snl_finalize_msg(&nw)) || !snl_send_message(&ss, hdr)) {
                            snl_free(&ss);
                            throw SysError("failed to sendoff netlink message");
                        }

                        struct snl_errmsg_data e = {};
                        snl_read_reply_code(&ss, hdr->nlmsg_seq, &e);
                        if (e.error_str != nullptr) {
                            snl_free(&ss);
                            throw SysError("failed to configure loopback interface: %1%", e.error_str);
                        }
                        snl_free(&ss);
                        _exit(0);
                    });

                    /* TODO: Capture the error from the helper? */
                    if (helper.wait() != 0) {
                        throw SysError("failed to configure loopback address");
                    }
                } else {
                    jid = jail_setv(
                        JAIL_CREATE,
                        "persist",
                        "true",
                        // 4 is the most restrictive devfs ruleset that meets our needs
                        // which is found in the default installation. Trying to add
                        // another one is a huge pain...
                        "devfs_ruleset",
                        "4",
                        "path",
                        chrootRootDir.c_str(),
                        "host.hostname",
                        "localhost",
                        "ip4",
                        "inherit",
                        "ip6",
                        "inherit",
                        "allow.raw_sockets",
                        "true",
                        nullptr);
                    if (jid < 0) {
                        throw SysError("failed to create jail (networked): %1%", jail_errmsg);
                    }
                    autoDelJail->jid = jid;
                }

                pid = startProcess([this,
                                    env,
                                    inputRewrites,
#  if NIX_WITH_AWS_AUTH
                                    awsCredentials,
#  endif
                                    jid]() mutable {
                    nix::setupPTYSlave(builderOut.get());

                    bool sendException = true;

                    try {
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

                        /* Close all other file descriptors. This must happen before
                           jail_attach for FreeBSD. */
                        unix::closeExtraFDs();

                        if (jail_attach(jid) < 0) {
                            throw SysError("failed to attach to jail");
                        }

                        if (chdir(tmpDirInSandbox().c_str()) == -1)
                            throw SysError("changing into %1%", PathFmt(tmpDir));

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
                [this](const std::filesystem::path & p) -> std::filesystem::path {
                    return chrootRootDir / p.relative_path();
                });
        }
    };

    DerivationBuilderUnique makeFreeBSDDerivationBuilder(
        LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
    {
        return makeGenericUnixDerivationBuilder(store, std::move(miscMethods), std::move(params));
    }

    DerivationBuilderUnique makeFreeBSDChrootDerivationBuilder(
        LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
    {
        return DerivationBuilderUnique(
            new FreeBSDChrootDerivationBuilder(store, std::move(miscMethods), std::move(params)));
    }

} // namespace nix

#endif
