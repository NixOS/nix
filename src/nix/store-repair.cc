#include "command.hh"
#include "store-api.hh"

using namespace nix;

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

    void run(ref<Store> store, std::vector<StorePath> && storePaths) override
    {
        for (auto & path : storePaths)
            store->repairPath(path);
    }
};

static auto rStoreRepair = registerCommand2<CmdStoreRepair>({"store", "repair"});
