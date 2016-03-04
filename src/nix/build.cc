#include "command.hh"
#include "common-args.hh"
#include "installables.hh"
#include "shared.hh"
#include "store-api.hh"

using namespace nix;

struct CmdBuild : StoreCommand, MixDryRun, MixInstallables
{
    CmdBuild()
    {
    }

    std::string name() override
    {
        return "build";
    }

    std::string description() override
    {
        return "build a derivation or fetch a store path";
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

        if (dryRun) return;

        store->buildPaths(pathsToBuild);
    }
};

static RegisterCommand r1(make_ref<CmdBuild>());
