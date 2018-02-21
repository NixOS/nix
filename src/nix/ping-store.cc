#include "command.hh"
#include "shared.hh"
#include "store-api.hh"

using namespace nix;

struct CmdPingStore : StoreCommand
{
    std::string name() override
    {
        return "ping-store";
    }

    std::string description() override
    {
        return "test whether a store can be opened";
    }

    Examples examples() override
    {
        return {
            Example{
                "To test whether connecting to a remote Nix store via SSH works:",
                "nix ping-store --store ssh://mac1"
            },
        };
    }

    void run(ref<Store> store) override
    {
        store->connect();
    }
};

static RegisterCommand r1(make_ref<CmdPingStore>());
