#include "nix/cmd/command.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/util/util.hh"

namespace nix {

struct CmdOptimiseStore : StoreCommand
{
    bool dryRun = false;

    CmdOptimiseStore()
    {
        addFlag({
            .longName = "dry-run",
            .description = "Report current dedup state and how much more an `optimise` pass "
                           "would save, without modifying the store.",
            .handler = {&dryRun, true},
        });
    }

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
        if (!dryRun) {
            store->optimiseStore();
            return;
        }

        auto maybeStats = store->queryStoreStats({});
        if (!maybeStats || !maybeStats->dedup || !maybeStats->predictedDedup)
            throw Error("`--dry-run` is not supported for this store");

        auto & d = *maybeStats->dedup;
        auto & p = *maybeStats->predictedDedup;
        notice("Currently saved by hard-linking:");
        notice("  Duplicate copies eliminated: %d", d.inodesSaved);
        notice("  Bytes saved:                 %s", renderSize(int64_t(d.dedupBytes)));
        notice("");
        notice("If `nix store optimise` were run now:");
        notice("  Additional files linkable:   %d", p.filesLinkable);
        notice("  Additional bytes freeable:   %s", renderSize(int64_t(p.bytesLinkable)));
    }
};

static auto rCmdOptimiseStore = registerCommand2<CmdOptimiseStore>({"store", "optimise"});

} // namespace nix
