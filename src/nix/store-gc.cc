#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/store/store-cast.hh"
#include "nix/store/gc-store.hh"
#include "nix/store/profiles.hh"

using namespace nix;

struct CmdStoreGC : StoreCommand, MixDryRun
{
    GCOptions options;
    std::optional<std::string> olderThan;

    CmdStoreGC()
    {
        addFlag({
            .longName = "max",
            .description = "Stop after freeing *n* bytes of disk space.",
            .labels = {"n"},
            .handler = {&options.maxFreed},
        });
        addFlag({
            .longName = "older-than",
            .description = "Only delete paths older than the specified age. *age* "
                           "must be in the format *N*`d`, where *N* denotes a number "
                           "of days.",
            .labels = {"age"},
            .handler = {&olderThan},
        });
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

        options.action = dryRun ? GCOptions::gcReturnDead : GCOptions::gcDeleteDead;
        options.olderThan = olderThan.transform(parseOlderThanTimeSpec);
        GCResults results;
        PrintFreed freed(options.action == GCOptions::gcDeleteDead, results);
        gcStore.collectGarbage(options, results);
    }
};

static auto rCmdStoreGC = registerCommand2<CmdStoreGC>({"store", "gc"});
