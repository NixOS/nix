#include "command.hh"
#include "common-args.hh"
#include "installables.hh"
#include "shared.hh"
#include "store-api.hh"
#include "derivations.hh"

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

        execlp("bash", "bash", nullptr);
    }
};

static RegisterCommand r1(make_ref<CmdRun>());
