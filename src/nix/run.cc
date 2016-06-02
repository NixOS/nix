#include "command.hh"
#include "common-args.hh"
#include "installables.hh"
#include "shared.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "local-store.hh"

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
            if (unshare(CLONE_NEWUSER | CLONE_NEWNS) == -1)
                throw SysError("setting up a private mount namespace");

            if (mount(store2->realStoreDir.c_str(), store->storeDir.c_str(), "", MS_BIND, 0) == -1)
                throw SysError(format("mounting ‘%s’ on ‘%s’") % store2->realStoreDir % store->storeDir);
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
