#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "store-cast.hh"
#include "gc-store.hh"

using namespace nix;

struct CmdStoreGC : StoreCommand
{
    CmdStoreGC()
    {
    }

    std::string description() override
    {
        return "perform garbage collection on a Nix store";
    }

    std::string doc() override
    {
        return
          #include "store-gc.md"
          ;
    }

    void run(ref<Store> store) override
    {
        auto & gcStore = require<GcStore>(*store);

        gcStore.doGC(true);
    }
};

static auto rCmdStoreGC = registerCommand2<CmdStoreGC>({"store", "gc"});
