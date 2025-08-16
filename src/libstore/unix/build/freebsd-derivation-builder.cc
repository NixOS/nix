#ifdef __FreeBSD__

#  include <stdlib.h>
#  include <string.h>

#  include <db.h>
#  include <net/if.h>
#  include <pwd.h>
#  include <sys/mount.h>
#  include <netlink/netlink.h>
#  include <netlink/netlink_route.h>
#  include <net/if.h>
#  include <sys/param.h>
#  include <sys/jail.h>
#  include <sys/sockio.h>
#  include <jail.h>

#  include "nix/util/freebsd-jail.hh"

namespace nix {

namespace {

struct PasswordEntry
{
    std::string name;
    uid_t uid;
    gid_t gid;
    std::string description;
    Path home;
    Path shell;
};

static void free_db(DB * db)
{
    if (db != nullptr) {
        (db->close)(db);
    }
}

// Database open flags from FreeBSD, in case they're necessary for compatibility
static const HASHINFO db_flags = {
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
    buf.reserve(buf.size() + str.size() + 1);
    buf.insert(buf.end(), str.begin(), str.end());
    buf.push_back(0);
}

static void serializeInt(std::vector<uint8_t> & buf, uint32_t num)
{
    buf.reserve(buf.size() + sizeof(num));
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
    // We can't use serializeString since that's null terimated
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

static void createPasswordFiles(Path & chrootRootDir, std::vector<PasswordEntry> & users)
{
    std::unique_ptr<DB, decltype(&free_db)> db(
        dbopen((chrootRootDir + "/etc/pwd.db").c_str(), O_CREAT | O_RDWR | O_EXCL, 0644, DB_HASH, &db_flags), &free_db);

    if (db == nullptr) {
        throw SysError("Could not create password database");
    }

    auto dbInsert = [&db](std::vector<uint8_t> key_buf, std::vector<uint8_t> & value_buf) {
        DBT key = {key_buf.data(), key_buf.size()};
        DBT value = {value_buf.data(), value_buf.size()};

        if ((db->put)(db.get(), &key, &value, R_NOOVERWRITE) == -1) {
            throw SysError("Could not write to password database");
        }
    };

    // Annoyingly DBT doesn't have const pointers so we need this whole shuffle
    std::string versionKeyStr(_PWD_VERSION_KEY);
    std::vector<uint8_t> versionKey(versionKeyStr.begin(), versionKeyStr.end());
    std::vector<uint8_t> versionValue{dbVersion};
    dbInsert(versionKey, versionValue);

    for (size_t i = 0; i < users.size(); i++) {
        auto user = users[i];

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
    std::string passwdContent = "";
    for (auto user : users) {
        passwdContent.append(
            fmt("%s:*:%d:%d:%s:%s:%s\n", user.name, user.uid, user.gid, user.description, user.home, user.shell));
    }

    writeFile(chrootRootDir + "/etc/passwd", passwdContent);

    // No need to make /etc/master.passwd or /etc/spwd.db,
    // our build user wouldn't be able to read them anyway
}

} // namespace

struct FreeBSDDerivationBuilder : virtual DerivationBuilderImpl
{
    using DerivationBuilderImpl::DerivationBuilderImpl;
};

template<size_t n>
struct iovec iovFromStaticSizedString(const char (&array)[n])
{
    return {
        .iov_base = const_cast<void *>(static_cast<const void *>(&array[0])),
        .iov_len = n,
    };
}

struct iovec iovFromDynamicSizeString(const std::string & s)
{
    return {
        .iov_base = const_cast<void *>(static_cast<const void *>(s.c_str())),
        .iov_len = s.length() + 1,
    };
}

struct ChrootFreeBSDDerivationBuilder : ChrootDerivationBuilder, FreeBSDDerivationBuilder
{
    /* Destructors happen in reverse order from declaration */
    std::shared_ptr<AutoRemoveJail> autoDelJail;
    std::vector<std::shared_ptr<AutoUnmount>> autoDelMounts;

    ChrootFreeBSDDerivationBuilder(
        Store & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
        : DerivationBuilderImpl{store, std::move(miscMethods), std::move(params)}
        , ChrootDerivationBuilder{store, std::move(miscMethods), std::move(params)}
        , FreeBSDDerivationBuilder{store, std::move(miscMethods), std::move(params)}
    {
    }

    void deleteTmpDir(bool force) override
    {
        /* Unmount and free jail id, if in use */
        autoDelMounts.clear();
        autoDelJail.reset();

        ChrootDerivationBuilder::deleteTmpDir(force);
    }

    void extraChrootParentDirCleanup(const Path & chrootParentDir) override
    {
        int count;
        struct statfs * mntbuf;
        if ((count = getmntinfo(&mntbuf, MNT_WAIT)) < 0) {
            throw SysError("Couldn't get mount info for chroot");
        }

        for (int i = 0; i < count; i++) {
            Path mounted(mntbuf[i].f_mntonname);
            if (hasPrefix(mounted, chrootParentDir)) {
                if (unmount(mounted.c_str(), 0) < 0) {
                    throw SysError("Failed to unmount path %1%", mounted);
                }
            }
        }
    }

    void prepareSandbox() override
    {
        ChrootDerivationBuilder::prepareSandbox();

        std::vector<PasswordEntry> users{
            {
                .name = "root",
                .uid = 0,
                .gid = 0,
                .description = "Nix build user",
                .home = settings.sandboxBuildDir,
                .shell = "/noshell",
            },
            {
                .name = "nixbld",
                .uid = buildUser->getUID(),
                .gid = sandboxGid(),
                .description = "Nix build user",
                .home = settings.sandboxBuildDir,
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
            chrootRootDir + "/etc/group",
            fmt("root:x:0:\n"
                "nixbld:!:%1%:\n"
                "nogroup:x:65534:\n",
                sandboxGid()));

        // Linux waits until after entering the child to start mounting so it doesn't
        // pollute the root mount namespace.
        // FreeBSD doesn't have mount namespaces, so there's no reason to wait.

        auto devpath = chrootRootDir + "/dev";
        mkdir(devpath.c_str(), 0555);
        mkdir((chrootRootDir + "/bin").c_str(), 0555);
        char errmsg[255] = "";
        struct iovec iov[8] = {
            iovFromStaticSizedString("fstype"),
            iovFromStaticSizedString("devfs"),
            iovFromStaticSizedString("fspath"),
            iovFromDynamicSizeString(devpath),
            iovFromStaticSizedString("ruleset"),
            iovFromStaticSizedString("4"),
            iovFromStaticSizedString("errmsg"),
            iovFromStaticSizedString(errmsg),
        };
        if (nmount(iov, 6, 0) < 0) {
            throw SysError("Failed to mount jail /dev: %1%", errmsg);
        }
        autoDelMounts.push_back(std::make_shared<AutoUnmount>(devpath));

        for (auto & i : pathsInChroot) {
            char errmsg[255];
            errmsg[0] = 0;

            if (i.second.source == "/proc") {
                continue; // backwards compatibility
            }
            auto path = chrootRootDir + i.first;

            struct stat stat_buf;
            if (stat(i.second.source.c_str(), &stat_buf) < 0) {
                throw SysError("stat");
            }

            // mount points must exist and be the right type
            if (S_ISDIR(stat_buf.st_mode)) {
                createDirs(path);
            } else {
                createDirs(dirOf(path));
                writeFile(path, "");
            }

            struct iovec iov[8] = {
                iovFromStaticSizedString("fstype"),
                iovFromStaticSizedString("nullfs"),
                iovFromStaticSizedString("fspath"),
                iovFromDynamicSizeString(path),
                iovFromStaticSizedString("target"),
                iovFromDynamicSizeString(i.second.source),
                iovFromStaticSizedString("errmsg"),
                iovFromStaticSizedString(errmsg),
            };
            if (nmount(iov, 8, 0) < 0) {
                throw SysError("Failed to mount nullfs for %1% - %2%", path, errmsg);
            }
            autoDelMounts.push_back(std::make_shared<AutoUnmount>(path));
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
            for (std::filesystem::path path : {"/etc/resolv.conf", "/etc/services", "/etc/hosts"}) {
                if (pathExists(path)) {
                    // TODO: Copy the actual file, not the symlink, because we don't know where
                    // the symlink is pointing, and we don't want to chase down the entire
                    // chain.
                    //
                    // This means if your network config changes during a FOD build,
                    // the DNS in the sandbox will be wrong. However, this is pretty unlikely
                    // to actually be a problem, because FODs are generally pretty fast,
                    // and machines with often-changing network configurations probably
                    // want to run resolved or some other local resolver anyway.
                    //
                    // There's also just no simple way to do this correctly, you have to manually
                    // inotify watch the files for changes on the outside and update the sandbox
                    // while the build is running (or at least that's what Flatpak does).
                    //
                    // I also just generally feel icky about modifying sandbox state under a build,
                    // even though it really shouldn't be a big deal. -K900
                    copyFile(path, std::filesystem::path{chrootRootDir} / path, false);
                }
            }

            if (settings.caFile != "" && pathExists(std::filesystem::path{settings.caFile.get()})) {
                // TODO: For the same reasons as above, copy the CA certificates file too.
                // It should be even less likely to change during the build than resolv.conf.
                createDirs(chrootRootDir + "/etc/ssl/certs");
                copyFile(
                    std::filesystem::path{settings.caFile.get()},
                    std::filesystem::path{chrootRootDir} + "/etc/ssl/certs/ca-certificates.crt",
                    false);
            }
        }
    }

    void enterChroot() override
    {
        int jid;

        if (derivationType.isSandboxed()) {
            jid = jail_setv(
                JAIL_CREATE | JAIL_ATTACH,
                "path",
                chrootRootDir.c_str(),
                "host.hostname",
                "localhost",
                // TODO: Make our own ruleset
                "vnet",
                "new",
                NULL);
            if (jid < 0) {
                throw SysError("Failed to create jail (isolated network)");
            }
            autoDelJail = std::make_shared<AutoRemoveJail>(jid);

            if (system(("ifconfig -j " + std::to_string(jid) + " lo0 inet 127.0.0.1/8 up").c_str()) != 0) {
                throw SysError("Failed to set up isolated network");
            }
        } else {
            jid = jail_setv(
                JAIL_CREATE | JAIL_ATTACH,
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
                NULL);
            if (jid < 0) {
                throw SysError("Failed to create jail (fixed-derivation)");
            }
            autoDelJail = std::make_shared<AutoRemoveJail>(jid);
        }

        if (derivationType.isSandboxed()) {
            AutoCloseFD fd(socket(PF_INET, SOCK_DGRAM, 0));
            if (!fd)
                throw SysError("cannot open IP socket");

            struct ifreq ifr;
            strcpy(ifr.ifr_name, "lo0");
            ifr.ifr_flags = IFF_UP | IFF_LOOPBACK;
            if (ioctl(fd.get(), SIOCSIFFLAGS, &ifr) == -1)
                throw SysError("cannot set loopback interface flags");

            AutoCloseFD netlink(socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE));

            struct
            {
                struct nlmsghdr nl_hdr;
                struct ifaddrmsg addr_msg;
                struct nlattr tl;
                uint8_t addr[4];
            } msg;

            // Many of the fields are deprecated or not useful to us,
            // just zero them all here
            memset(&msg, 0, sizeof(msg));

            msg.nl_hdr.nlmsg_len = sizeof(msg);
            msg.nl_hdr.nlmsg_type = NL_RTM_NEWADDR;
            msg.nl_hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;

            msg.addr_msg.ifa_family = AF_INET;
            msg.addr_msg.ifa_prefixlen = 8;
            msg.addr_msg.ifa_index = if_nametoindex("lo0");

            msg.tl.nla_len = sizeof(struct nlattr) + 4;
            msg.tl.nla_type = IFLA_ADDRESS;
            memcpy(msg.addr, new uint8_t[]{127, 0, 0, 1}, 4);

            send(netlink.get(), (void *) &msg, sizeof(msg), 0);

            struct
            {
                struct nlmsghdr nl_hdr;
                struct nlmsgerr err;
            } response;

            size_t n = recv(netlink.get(), &response, sizeof(response), 0);

            if (n < sizeof(response) || response.nl_hdr.nlmsg_type != NLMSG_ERROR) {
                throw SysError("Invalid repsonse when setting loopback interface address");
            } else if (response.err.error != 0) {
                throw SysError(response.err.error, "Could not set loopback interface address");
            }
        }
    }

    void startChild() override {}

    void addDependency(const StorePath & path) override
    {
        auto [source, target] = ChrootDerivationBuilder::addDependencyPrep(path);

        warn("Not yet implemented, dependency not added inside sandbox");
    }
};

} // namespace nix

#endif
