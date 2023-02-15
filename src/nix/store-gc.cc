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
        /* The default threshold of 0 makes sense for auto-GC, but not
           when the garbage collector is invoked manually. */
        settings.minFree.setDefault(std::numeric_limits<uint64_t>::max());

        auto & gcStore = require<GcStore>(*store);

        gcStore.autoGC(true);
    }
};

static auto rCmdStoreGC = registerCommand2<CmdStoreGC>({"store", "gc"});
