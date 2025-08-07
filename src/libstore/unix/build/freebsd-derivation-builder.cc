#ifdef __FreeBSD__

#  include <sys/param.h>
#  include <sys/jail.h>
#  include <jail.h>
#  include <stdlib.h>
#  include <sys/mount.h>
#  include <string.h>
#  include "nix/util/freebsd-jail.hh"

namespace nix {

struct FreeBSDDerivationBuilder : virtual DerivationBuilderImpl
{
    using DerivationBuilderImpl::DerivationBuilderImpl;
};

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
        auto devpath = chrootRootDir + "/dev";
        mkdir(devpath.c_str(), 0555);
        mkdir((chrootRootDir + "/bin").c_str(), 0555);
        char errmsg[255] = "";
        struct iovec iov[8] = {
            {.iov_base = (void *) "fstype", .iov_len = sizeof("fstype")},
            {.iov_base = (void *) "devfs", .iov_len = sizeof("devfs")},
            {.iov_base = (void *) "fspath", .iov_len = sizeof("fspath")},
            {.iov_base = (void *) devpath.c_str(), .iov_len = devpath.length() + 1},
            {.iov_base = (void *) "errmsg", .iov_len = sizeof("errmsg")},
            {.iov_base = (void *) errmsg, .iov_len = sizeof(errmsg)},
        };
        if (nmount(iov, 6, 0) < 0) {
            throw SysError("Failed to mount jail /dev: %1%", errmsg);
        }
        autoDelMounts.push_back(std::make_shared<AutoUnmount>(devpath));

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
                    pathsInChroot.try_emplace(path, path, true);

            if (settings.caFile != "")
                pathsInChroot.try_emplace("/etc/ssl/certs/ca-certificates.crt", settings.caFile, true);
        }

        for (auto & i : pathsInChroot) {
            char errmsg[255];
            errmsg[0] = 0;

            if (i.second.source == "/proc")
                continue; // backwards compatibility
            auto path = chrootRootDir + i.first;

            struct stat stat_buf;
            if (stat(i.second.source.c_str(), &stat_buf) < 0) {
                throw SysError("stat");
            }

            // mount points must exist and be the right type
            if (S_ISDIR(stat_buf.st_mode)) {
                mkdir(path.c_str(), 0555);
            } else {
                close(open(path.c_str(), O_CREAT | O_RDONLY, 0444));
            }

            struct iovec iov[8] = {
                {.iov_base = (void *) "fstype", .iov_len = sizeof("fstype")},
                {.iov_base = (void *) "nullfs", .iov_len = sizeof("nullfs")},
                {.iov_base = (void *) "fspath", .iov_len = sizeof("fspath")},
                {.iov_base = (void *) path.c_str(), .iov_len = path.length() + 1},
                {.iov_base = (void *) "target", .iov_len = sizeof("target")},
                {.iov_base = (void *) i.second.source.c_str(), .iov_len = i.second.source.length() + 1},
                {.iov_base = (void *) "errmsg", .iov_len = sizeof("errmsg")},
                {.iov_base = (void *) errmsg, .iov_len = sizeof(errmsg)},
            };
            if (nmount(iov, 8, 0) < 0) {
                throw SysError("Failed to mount nullfs for %1% - %2%", path, errmsg);
            }
            autoDelMounts.push_back(std::make_shared<AutoUnmount>(path));
        }
    }

    void startChild() override
    {
        /* Now that we now the sandbox uid, we can write
           /etc/passwd. */
        writeFile(
            chrootRootDir + "/etc/passwd",
            fmt("root:x:0:0::::Nix build user:%3%:/noshell\n"
                "nixbld:x:%1%:%2%::::Nix build user:%3%:/noshell\n"
                "nobody:x:65534:65534::::Nobody:/:/noshell\n",
                buildUser->getUID(),
                sandboxGid(),
                settings.sandboxBuildDir));
        if (system(("pwd_mkdb -d " + chrootRootDir + "/etc " + chrootRootDir + "/etc/passwd 2>/dev/null").c_str())
            != 0) {
            throw SysError("Failed to set up isolated users");
        }

        int jid;

        if (derivationType.isSandboxed()) {
            jid = jail_setv(
                JAIL_CREATE,
                "persist",
                "true",
                "path",
                chrootRootDir.c_str(),
                "devfs_ruleset",
                "4",
                "vnet",
                "new",
                "host.hostname",
                "nixbsd",
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
                JAIL_CREATE,
                "persist",
                "true",
                "path",
                chrootRootDir.c_str(),
                "devfs_ruleset",
                "4",
                "ip4",
                "inherit",
                "ip6",
                "inherit",
                "allow.raw_sockets",
                "true",
                "host.hostname",
                "nixbsd",
                NULL);
            if (jid < 0) {
                throw SysError("Failed to create jail (fixed-derivation)");
            }
            autoDelJail = std::make_shared<AutoRemoveJail>(jid);
        }

        pid = startProcess([&]() {
            openSlave();
            if (jail_attach(jid) < 0) {
                throw SysError("Failed to attach to jail");
            }
            runChild();
        });
    }

    void addDependency(const StorePath & path) override
    {
        auto [source, target] = ChrootDerivationBuilder::addDependencyPrep(path);

        warn("Not yet implemented, dependency not added inside sandbox");
    }
};

} // namespace nix

#endif
