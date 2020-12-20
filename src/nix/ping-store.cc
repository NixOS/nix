#include "command.hh"
#include "shared.hh"
#include "store-api.hh"

using namespace nix;

struct CmdPingStore : StoreCommand
{
    std::string description() override
    {
        return "test whether a store can be opened";
    }

    Examples examples() override
    {
        return {
            Example{
                "To test whether connecting to a remote Nix store via SSH works:",
                "nix store ping --store ssh://mac1"
            },
        };
    }

    void run(ref<Store> store) override
    {
        store->connect();
    }
};

static auto rCmdPingStore = registerCommand2<CmdPingStore>({"store", "ping"});
