#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"

using namespace nix;

struct CmdBuild : MixDryRun, InstallablesCommand
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
        auto paths = toStorePaths(store, dryRun ? DryRun : Build);

        printInfo("build result: %s", showPaths(paths));
    }
};

static RegisterCommand r1(make_ref<CmdBuild>());
