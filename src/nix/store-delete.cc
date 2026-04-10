#include "nix/cmd/command.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/store/store-cast.hh"
#include "nix/store/gc-store.hh"

namespace nix {

struct CmdStoreDelete : StorePathsCommand
{
    GCOptions options{.action = GCOptions::gcDeleteSpecific};

    CmdStoreDelete()
    {
        addFlag({
            .longName = "ignore-liveness",
            .description = "Do not check whether the paths are reachable from a root.",
            .handler = {&options.ignoreLiveness, true},
        });

        addFlag({
            .longName = "skip-alive",
            .description =
                "Do not emit errors when attempting to delete something that is still alive, useful with --recursive.",
            .handler = {&options.action, GCOptions::gcDeleteDead},
        });
    }

    std::string description() override
    {
        return "delete paths from the Nix store";
    }

    std::string doc() override
    {
        return
#include "store-delete.md"
            ;
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        auto & gcStore = require<GcStore>(*store);

        StorePathSet paths;
        for (auto & path : storePaths)
            paths.insert(path);
        options.pathsToDelete = std::move(paths);

        GCResults results;
        Finally printer([&] { printFreed(false, results); });
        gcStore.collectGarbage(options, results);
    }
};

static auto rCmdStoreDelete = registerCommand2<CmdStoreDelete>({"store", "delete"});

} // namespace nix
