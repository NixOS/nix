#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/store/store-cast.hh"
#include "nix/store/gc-store.hh"
#include "nix/util/error.hh"
#include "nix/store/profiles.hh"

namespace nix {

struct CmdStoreGC : StoreCommand, MixDryRun
{
    GCOptions options;
    std::optional<std::string> olderThan;

    CmdStoreGC()
    {
        addFlag({
            .longName = "max",
            .description = "Stop after freeing *n* bytes of disk space. Cannot be combined with --dry-run.",
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
        if (options.maxFreed != std::numeric_limits<uint64_t>::max() && dryRun)
            throw UsageError("options --max and --dry-run cannot be combined");

        auto & gcStore = require<GcStore>(*store);

        options.action = dryRun ? GCOptions::gcReturnDead : GCOptions::gcDeleteDead;
        options.pathsToDelete = GCOptions::WholeStore{};
        options.olderThan = olderThan.transform(parseOlderThanTimeSpec);
        GCResults results;
        Finally printer([&] { printFreed(dryRun, results); });
        gcStore.collectGarbage(options, results);
    }
};

static auto rCmdStoreGC = registerCommand2<CmdStoreGC>({"store", "gc"});

} // namespace nix
