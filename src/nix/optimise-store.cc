#include "nix/cmd/command.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"

#include <atomic>

using namespace nix;

struct CmdOptimiseStore : StoreCommand
{
    std::string description() override
    {
        return "replace identical files in the store by hard links";
    }

    std::string doc() override
    {
        return
          #include "optimise-store.md"
          ;
    }

    void run(ref<Store> store) override
    {
        store->optimiseStore();
    }
};

static auto rCmdOptimiseStore = registerCommand2<CmdOptimiseStore>({"store", "optimise"});
