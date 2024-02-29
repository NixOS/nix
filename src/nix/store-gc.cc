#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "store-cast.hh"
#include "gc-store.hh"

using namespace nix;

struct CmdStoreGC : InstallablesCommand, MixDryRun
{
    GCOptions options;

    CmdStoreGC()
    {
        addFlag({
            .longName = "max",
            .description = "Stop after freeing *n* bytes of disk space.",
            .labels = {"n"},
            .handler = {&options.maxFreed}
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

    // Don't add a default installable if none is specified so that
    // `nix store gc` runs a full gc
    void applyDefaultInstallables(std::vector<std::string> & rawInstallables) override {
    }

    void run(ref<Store> store, Installables && installables) override
    {
        auto & gcStore = require<GcStore>(*store);

        options.action = dryRun ? GCOptions::gcReturnDead : GCOptions::gcDeleteDead;

        // Add the closure of the installables to the set of paths to delete.
        // If there's no installable specified, this will leave an empty set
        // of paths to delete, which means the whole store will be gc-ed.
        StorePathSet closureRoots;
        for (auto & i : installables) {
            try {
                auto installableOutPath = Installable::toStorePath(getEvalStore(), store, Realise::Derivation, OperateOn::Output, i);
                if (store->isValidPath(installableOutPath)) {
                    closureRoots.insert(installableOutPath);
                }
            } catch (MissingRealisation &) {
            }
        }
        store->computeFSClosure(closureRoots, options.pathsToDelete);
        GCResults results;
        PrintFreed freed(options.action == GCOptions::gcDeleteDead, results);
        gcStore.collectGarbage(options, results);
    }
};

static auto rCmdStoreGC = registerCommand2<CmdStoreGC>({"store", "gc"});
