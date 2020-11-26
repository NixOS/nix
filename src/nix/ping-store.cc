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
                "nix ping-store --store ssh://mac1"
            },
        };
    }

    Category category() override { return catUtility; }

    void run(ref<Store> store) override
    {
        store->connect();
    }
};

static auto rCmdPingStore = registerCommand<CmdPingStore>("ping-store");
