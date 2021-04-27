#include "command.hh"
#include "shared.hh"
#include "store-api.hh"

using namespace nix;

struct CmdPingStore : StoreCommand
{
    std::string description() override
    {
        return "test whether a store can be accessed";
    }

    std::string doc() override
    {
        return
          #include "ping-store.md"
          ;
    }

    void run(ref<Store> store) override
    {
        store->connect();
    }
};

static auto rCmdPingStore = registerCommand2<CmdPingStore>({"store", "ping"});
