#include "nix/cmd/command.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/util/util.hh"

#include <nlohmann/json.hpp>

namespace nix {

struct CmdStoreStats : StoreCommand, MixJSON
{
    std::string description() override
    {
        return "show statistics about the contents of a Nix store";
    }

    std::string doc() override
    {
        return
#include "store-stats.md"
            ;
    }

    void run(ref<Store> store) override
    {
        auto stats = store->queryStoreStats();
        if (!stats)
            throw Error(
                "store '%s' does not provide statistics", store->config.getReference().render(/*withParams=*/true));

        if (json) {
            printJSON(*stats);
            return;
        }

        auto saved = [](const nlohmann::json & s) {
            return fmt("%s (%d files)", renderSize(s.at("bytes").get<int64_t>()), s.at("files").get<uint64_t>());
        };
        notice("Valid paths: %d", stats->at("pathCount").get<uint64_t>());
        notice("Saved by hard-linking: %s", saved(stats->at("dedup")));
        notice("Would be saved by `nix store optimise`: %s", saved(stats->at("predictedDedup")));
    }
};

static auto rCmdStoreStats = registerCommand2<CmdStoreStats>({"store", "stats"});

} // namespace nix
