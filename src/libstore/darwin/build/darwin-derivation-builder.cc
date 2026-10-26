#include "unix-derivation-builder-impl.hh"
#include "darwin-derivation-builder.hh"

#include <map>
#include <spawn.h>
#include <sys/sysctl.h>
#include <sandbox.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>

/* This definition is undocumented but depended upon by all major browsers. */
extern "C" int
sandbox_init_with_parameters(const char * profile, uint64_t flags, const char * const parameters[], char ** errorbuf);

/* Darwin IPC structures and constants */
#define IPCS_MAGIC 0x00000001
#define IPCS_SHM_ITER 0x00000002
#define IPCS_SEM_ITER 0x00000020
#define IPCS_MSG_ITER 0x00000200
#define IPCS_SHM_SYSCTL "kern.sysv.ipcs.shm"
#define IPCS_MSG_SYSCTL "kern.sysv.ipcs.msg"
#define IPCS_SEM_SYSCTL "kern.sysv.ipcs.sem"

struct IpcsCommand
{
    uint32_t ipcs_magic;
    uint32_t ipcs_op;
    uint32_t ipcs_cursor;
    uint32_t ipcs_datalen;
    void * ipcs_data;
};

namespace nix {

void DarwinDerivationBuilder::prepareSandbox()
{
    pathsInChroot = getPathsInSandbox();
}

void DarwinDerivationBuilder::setUser()
{
    UnixDerivationBuilderImpl::setUser();

    /* This has to appear before import statements. */
    std::string sandboxProfile = "(version 1)\n";

    if (useSandbox) {

        /* Lots and lots and lots of file functions freak out if they can't stat their full ancestry */
        StringSet ancestry;

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

        /* And we want the store in there regardless of how empty pathsInChroot. We include the innermost
           path component this time, since it's typically /nix/store and we care about that. */
        std::filesystem::path cur = store->storeDir;
        while (cur != "/") {
            ancestry.insert(cur.native());
            cur = cur.parent_path();
        }

        /* Add all our input paths to the chroot */
        for (auto & i : inputPaths) {
            auto p = store->printStorePath(i);
            pathsInChroot.insert_or_assign(p, ChrootPath{.source = p});
        }

        /* Violations will go to the syslog if you set this. Unfortunately the destination does not appear to be
         * configurable */
        if (store->getLocalSettings().darwinLogSandboxViolations) {
            sandboxProfile += "(deny default)\n";
        } else {
            sandboxProfile += "(deny default (with no-log))\n";
        }

        sandboxProfile +=
#include "sandbox-defaults.sb"
            ;

        if (!derivationType.isSandboxed())
            sandboxProfile +=
#include "sandbox-network.sb"
                ;

        /* Add the output paths we'll use at build-time to the chroot */
        sandboxProfile += "(allow file-read* file-write* process-exec\n";
        for (auto & [_, path] : scratchOutputs)
            sandboxProfile += fmt("\t(subpath \"%s\")\n", store->printStorePath(path));

        sandboxProfile += ")\n";

        /* Our inputs (transitive dependencies and any impurities computed above)
           without file-write* allowed, access() incorrectly returns EPERM.

           The sandbox compiler merges all path filters of an operation into a single
           pattern table that must stay below 64 KiB, regardless of how many (allow ...)
           forms they are spread over (see https://github.com/NixOS/nix/issues/4119).
           A group guarded by a distinct (prefix ...) filter gets its own table, so we
           bucket store paths by the first character of their hash. The guard is a
           superset of every filter in its group, so it does not change what is allowed. */

        std::map<char, std::string> bucketedStorePathProfiles;
        std::string otherSandboxPathsProfile;

        for (auto & i : pathsInChroot) {
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

            /* Store paths are bucketed by the store path hash'es first
               character, creating 32 buckets. This allows us to fit around 10k
               store paths into the sandbox. Everything else (impure deps or
               manually specified sandbox-paths) is bundled into a separate
               clause.

               One step further would be to use two characters for the prefix, yielding
               1024 buckets. However, we're already seeing the "profile too large" error
               instead of the "data object length <> exceeds maximum (65535)" error at 12k
               paths, so we aren't bottlenecked by the number of buckets. */

            /* For future: One additional trick we can pull here is to reduce the path to a
               (prefix "$storePrefix/<hash>-"), which is shorter than either a subpath, or
               a literal value. It should be safe as hashes are unique. */
            std::string profileRule =
                S_ISDIR(optSt->st_mode) ? fmt("\t\t(subpath \"%s\")\n", path) : fmt("\t\t(literal \"%s\")\n", path);

            /* Legitimate derivation input paths are already lexically canonical. */
            if (store->isInStore(path)) {
                /* Anything that's inside the store must also be a valid StorePath (or a path under it).
                   Any other path under the store directory is an implementation detail of Nix.
                   (currently that's only .links/ directory). */
                auto [sp, subdir] = store->toStorePath(path);
                /* Bucket by the nix32 first char, which is uniformly distributed in the long run. */
                bucketedStorePathProfiles[sp.hashPart().front()] += profileRule;
            } else {
                otherSandboxPathsProfile += profileRule;
            }
        }

        if (!otherSandboxPathsProfile.empty()) /* Avoid a repeat of CVE-2026-110451. */
            sandboxProfile += fmt("(allow file-read* file-write* process-exec\n%s)\n", otherSandboxPathsProfile);

        const auto storePrefix = store->storeDir + '/';
        for (const auto & [bucket, filters] : bucketedStorePathProfiles) {
            assert(!filters.empty()); /* Just to be safe. */
            const auto prefix = storePrefix + bucket;
            sandboxProfile +=
                fmt("(allow file-read* file-write* process-exec\n"
                    "\t(require-all (prefix \"%s\")\n"
                    "\t\t(require-any\n%s)))\n",
                    prefix,
                    filters);
        }

        /* Allow file-read* on full directory hierarchy to self. Allows realpath() */
        sandboxProfile += "(allow file-read*\n";
        assert(!ancestry.empty()); /* Just to be safe. */
        for (auto & i : ancestry) {
            sandboxProfile += fmt("\t(literal \"%s\")\n", i);
        }
        sandboxProfile += ")\n";

        sandboxProfile += drvOptions.additionalSandboxProfile;
    } else
        sandboxProfile +=
#include "sandbox-minimal.sb"
            ;

    debug("Generated sandbox profile:");
    debug(sandboxProfile);

    /* The tmpDir in scope points at the temporary build directory for our derivation. Some packages try different
       mechanisms to find temporary directories, so we want to open up a broader place for them to put their files,
       if needed. */
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
                sandboxProfile.c_str(), 0, stringsToCharPtrs(sandboxArgs).data(), &sandbox_errbuf)) {
            writeFull(
                STDERR_FILENO, fmt("failed to configure sandbox: %s\n", sandbox_errbuf ? sandbox_errbuf : "(null)"));
            _exit(1);
        }
    }
}

void DarwinDerivationBuilder::execBuilder(const Strings & args, const Strings & envStrs)
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
        NULL, drv.builder.c_str(), NULL, &attrp, stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());
}

void DarwinDerivationBuilder::cleanupSysVIPCForUser(uid_t uid)
{
    struct IpcsCommand ic;
    size_t ic_size = sizeof(ic);
    // IPC ids to cleanup
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

void DarwinDerivationBuilder::killSandbox(bool getStats)
{
    UnixDerivationBuilderImpl::killSandbox(getStats);
    if (buildUser) {
        auto uid = buildUser->getUID();
        cleanupSysVIPCForUser(uid);
    }
}

} // namespace nix
