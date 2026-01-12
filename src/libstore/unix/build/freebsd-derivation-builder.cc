#ifdef __FreeBSD__

#  include <cstdlib>
#  include <cstring>

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
    std::filesystem::path home;
    std::filesystem::path shell;
};

using OwnedDB = std::unique_ptr<::DB, decltype([](::DB * db) {
                                    if (db != nullptr) {
                                        (db->close)(db);
                                    }
                                })>;

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

static void createPasswordFiles(std::filesystem::path & chrootRootDir, std::vector<PasswordEntry> & users)
{
    OwnedDB db{dbopen((chrootRootDir / "etc/pwd.db").c_str(), O_CREAT | O_RDWR | O_EXCL, 0644, DB_HASH, &db_flags)};

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

    writeFile(chrootRootDir / "etc/passwd", passwdContent);

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
    AutoRemoveJail autoDelJail;
    std::vector<AutoUnmount> autoDelMounts;

    ChrootFreeBSDDerivationBuilder(
        LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
        : DerivationBuilderImpl{store, std::move(miscMethods), std::move(params)}
        , ChrootDerivationBuilder{store, std::move(miscMethods), std::move(params)}
        , FreeBSDDerivationBuilder{store, std::move(miscMethods), std::move(params)}
    {
    }

    void cleanupBuild(bool force) override
    {
        /* Unmount and free jail id, if in use */
        autoDelMounts.clear();
        autoDelJail.cancel();

        ChrootDerivationBuilder::cleanupBuild(force);
    }

    void extraChrootParentDirCleanup(const std::filesystem::path & chrootParentDir) override
    {
        int count;
        struct statfs * mntbuf;
        if ((count = getmntinfo(&mntbuf, MNT_WAIT)) < 0) {
            throw SysError("Couldn't get mount info for chroot");
        }

        for (const struct statfs & st : std::span(mntbuf, count)) {
            std::filesystem::path mounted = st.f_mntonname;
            if (isInDir(mounted, chrootParentDir)) {
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
                .home = std::filesystem::path{settings.sandboxBuildDir.get()},
                .shell = std::filesystem::path{"/noshell"},
            },
            {
                .name = "nixbld",
                .uid = buildUser->getUID(),
                .gid = sandboxGid(),
                .description = "Nix build user",
                .home = std::filesystem::path{settings.sandboxBuildDir.get()},
                .shell = std::filesystem::path{"/noshell"},
            },
            {
                .name = "nobody",
                .uid = 65534,
                .gid = 65534,
                .description = "Nobody",
                .home = std::filesystem::path{"/"},
                .shell = std::filesystem::path{"/noshell"},
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
        mkdir(devpath.c_str(), 0555);
        mkdir((chrootRootDir / "bin").c_str(), 0555);
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
        autoDelMounts.push_back(AutoUnmount{devpath});

        for (auto & i : pathsInChroot) {
            char errmsg[255];
            errmsg[0] = 0;

            if (i.second.source == "/proc") {
                continue; // backwards compatibility
            }
            auto path = chrootRootDir / i.first;

            struct stat stat_buf;
            if (stat(i.second.source.c_str(), &stat_buf) < 0) {
                throw SysError("stat");
            }

            // mount points must exist and be the right type
            if (S_ISDIR(stat_buf.st_mode)) {
                createDirs(path);
            } else {
                createDirs(path.parent_path());
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
            autoDelMounts.push_back(AutoUnmount{path});
        }

        /* Fixed-output derivations typically need to access the
           network, so give them access to /etc/resolv.conf and so
           on. */
        if (!derivationType.isSandboxed()) {
            // Only use nss functions to resolve hosts and
            // services. Don’t use it for anything else that may
            // be configured for this system. This limits the
            // potential impurities introduced in fixed-outputs.
            writeFile(chrootRootDir / "etc/nsswitch.conf", "hosts: files dns\nservices: files\n");

            /* N.B. it is realistic that these paths might not exist. It
               happens when testing Nix building fixed-output derivations
               within a pure derivation. */
            for (std::filesystem::path path : {"/etc/resolv.conf", "/etc/services", "/etc/hosts"}) {
                if (pathExists(path)) {
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
                    copyFile(path, chrootRootDir / path.relative_path(), /*andDelete=*/false, /*contents=*/true);
                }
            }

            if (settings.caFile != "" && pathExists(std::filesystem::path{settings.caFile.get()})) {
                // For the same reasons as above, copy the CA certificates file too.
                // It should be even less likely to change during the build than resolv.conf.
                createDirs(chrootRootDir / "etc/ssl/certs");
                copyFile(
                    std::filesystem::path{settings.caFile.get()},
                    chrootRootDir / "etc/ssl/certs/ca-certificates.crt",
                    /*andDelete=*/false,
                    /*contents=*/true);
            }
        }
    }

    void startChild() override
    {
        RunChildArgs args{
#  if NIX_WITH_AWS_AUTH
            .awsCredentials = preResolveAwsCredentials(),
#  endif
        };

        if (derivationType.isSandboxed()) {
            {
                int jid = jail_setv(
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
                    NULL);
                if (jid < 0) {
                    throw SysError("Failed to create jail (isolated network): %1%", jail_errmsg);
                }
                autoDelJail = {jid};
            }

            // Everything from here to the end of the block is setting up the network
            AutoCloseFD fd(socket(PF_INET, SOCK_DGRAM, 0));
            if (!fd)
                throw SysError("cannot open IP socket");

            struct ifreq ifr;
            strcpy(ifr.ifr_name, "lo0");
            ifr.ifr_flags = IFF_UP | IFF_LOOPBACK;
            if (ioctl(fd.get(), SIOCSIFFLAGS, &ifr) == -1)
                throw SysError("cannot set loopback interface flags");

            AutoCloseFD netlink(socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE));

            using IPv4 = std::array<uint8_t, 4>;

            struct
            {
                struct nlmsghdr nl_hdr;
                struct ifaddrmsg addr_msg;
                struct nlattr tl;
                IPv4 addr;
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
            msg.addr = IPv4{127, 0, 0, 1};

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
        } else {
            int jid = jail_setv(
                JAIL_CREATE,
                "persist",
                "true",
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
                NULL);
            if (jid < 0) {
                throw SysError("Failed to create jail (networked): %1%", jail_errmsg);
            }
            autoDelJail = {jid};
        }

        pid = startProcess([&]() {
            openSlave();
            runChild(args);
        });
    }

    void enterChroot() override
    {
        if (jail_attach(autoDelJail) < 0) {
            throw SysError("Failed to attach to jail");
        }
    }

    void addDependency(const StorePath & path)
    {
        auto [source, target] = ChrootDerivationBuilder::addDependencyPrep(path);
        throw UnimplementedError("'recursive-nix' is not supported on FreeBSD");
    }
};

} // namespace nix

#endif
