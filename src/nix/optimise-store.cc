#include "command.hh"
#include "shared.hh"
#include "store-api.hh"

#include <atomic>

using namespace nix;

struct CmdOptimiseStore : StoreCommand
{
    CmdOptimiseStore()
    {
    }

    std::string name() override
    {
        return "optimise-store";
    }

    std::string description() override
    {
        return "replace identical files in the store by hard links";
    }

    Examples examples() override
    {
        return {
            Example{
                "To optimise the Nix store:",
                "nix optimise-store"
            },
        };
    }

    void run(ref<Store> store) override
    {
        store->optimiseStore();
    }
};

static RegisterCommand r1(make_ref<CmdOptimiseStore>());
