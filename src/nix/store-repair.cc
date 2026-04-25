#include "nix/cmd/command.hh"
#include "nix/store/store-api.hh"
#include "nix/store/build.hh"

namespace nix {

struct CmdStoreRepair : StorePathsCommand
{
    std::string description() override
    {
        return "repair store paths";
    }

    std::string doc() override
    {
        return
#include "store-repair.md"
            ;
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        for (auto & path : storePaths)
            getDefaultBuilder(store)->repairPath(path);
    }
};

static auto rStoreRepair = registerCommand2<CmdStoreRepair>({"store", "repair"});

} // namespace nix
