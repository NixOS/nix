#include "command.hh"
#include "common-args.hh"
#include "installables.hh"
#include "shared.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "local-store.hh"
#include "finally.hh"

#if __linux__
#include <sys/mount.h>
#endif

using namespace nix;

struct CmdRun : StoreCommand, MixInstallables
{
    CmdRun()
    {
    }

    std::string name() override
    {
        return "run";
    }

    std::string description() override
    {
        return "run a shell in which the specified packages are available";
    }

    void run(ref<Store> store) override
    {
        auto elems = evalInstallables(store);

        PathSet pathsToBuild;

        for (auto & elem : elems) {
            if (elem.isDrv)
                pathsToBuild.insert(elem.drvPath);
            else
                pathsToBuild.insert(elem.outPaths.begin(), elem.outPaths.end());
        }

        printMissing(store, pathsToBuild);

        store->buildPaths(pathsToBuild);

        auto store2 = store.dynamic_pointer_cast<LocalStore>();

        if (store2 && store->storeDir != store2->realStoreDir) {
#if __linux__
            uid_t uid = getuid();
            uid_t gid = getgid();

            if (unshare(CLONE_NEWUSER | CLONE_NEWNS) == -1)
                throw SysError("setting up a private mount namespace");

            /* Bind-mount realStoreDir on /nix/store. If the latter
               mount point doesn't already exists, we have to create a
               chroot environment containing the mount point and bind
               mounts for the children of /. Would be nice if we could
               use overlayfs here, but that doesn't work in a user
               namespace yet (Ubuntu has a patch for this:
               https://bugs.launchpad.net/ubuntu/+source/linux/+bug/1478578). */
            if (!pathExists(store->storeDir)) {
                // FIXME: Use overlayfs?

                Path tmpDir = createTempDir();

                createDirs(tmpDir + store->storeDir);

                if (mount(store2->realStoreDir.c_str(), (tmpDir + store->storeDir).c_str(), "", MS_BIND, 0) == -1)
                    throw SysError(format("mounting ‘%s’ on ‘%s’") % store2->realStoreDir % store->storeDir);

                for (auto entry : readDirectory("/")) {
                    Path dst = tmpDir + "/" + entry.name;
                    if (pathExists(dst)) continue;
                    if (mkdir(dst.c_str(), 0700) == -1)
                        throw SysError(format("creating directory ‘%s’") % dst);
                    if (mount(("/" + entry.name).c_str(), dst.c_str(), "", MS_BIND | MS_REC, 0) == -1)
                        throw SysError(format("mounting ‘%s’ on ‘%s’") %  ("/" + entry.name) % dst);
                }

                char * cwd = getcwd(0, 0);
                if (!cwd) throw SysError("getting current directory");
                Finally freeCwd([&]() { free(cwd); });

                if (chroot(tmpDir.c_str()) == -1)
                    throw SysError(format("chrooting into ‘%s’") % tmpDir);

                if (chdir(cwd) == -1)
                    throw SysError(format("chdir to ‘%s’ in chroot") % cwd);
            } else
                if (mount(store2->realStoreDir.c_str(), store->storeDir.c_str(), "", MS_BIND, 0) == -1)
                    throw SysError(format("mounting ‘%s’ on ‘%s’") % store2->realStoreDir % store->storeDir);

            writeFile("/proc/self/setgroups", "deny");
            writeFile("/proc/self/uid_map", (format("%d %d %d") % uid % uid % 1).str());
            writeFile("/proc/self/gid_map", (format("%d %d %d") % gid % gid % 1).str());
#else
            throw Error(format("mounting the Nix store on ‘%s’ is not supported on this platform") % store->storeDir);
#endif
        }

        PathSet outPaths;
        for (auto & path : pathsToBuild)
            if (isDerivation(path)) {
                Derivation drv = store->derivationFromPath(path);
                for (auto & output : drv.outputs)
                    outPaths.insert(output.second.path);
            } else
                outPaths.insert(path);

        auto unixPath = tokenizeString<Strings>(getEnv("PATH"), ":");
        for (auto & path : outPaths)
            if (pathExists(path + "/bin"))
                unixPath.push_front(path + "/bin");
        setenv("PATH", concatStringsSep(":", unixPath).c_str(), 1);

        if (execlp("bash", "bash", nullptr) == -1)
            throw SysError("unable to exec ‘bash’");
    }
};

static RegisterCommand r1(make_ref<CmdRun>());
