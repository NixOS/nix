#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/store/store-cast.hh"
#include "nix/store/gc-store.hh"
#include "nix/util/error.hh"
#include "nix/util/logging.hh"

using namespace nix;

struct CmdStoreGCClosure : InstallablesCommand
{
    GCOptions options;

    std::string description() override
    {
        return "perform garbage collection on a Nix store within a closure";
    }

    std::string doc() override
    {
        return
#include "store-gc-closure.md"
            ;
    }

    void run(ref<Store> store, Installables && installables) override
    {
        auto & gcStore = require<GcStore>(*store);

        options.action = GCOptions::gcDeleteDead;

        // Add the closure of the installables to the set of paths to delete.
        // If there's no installable specified, this will leave an empty set
        // of paths to delete, which means the whole store will be gc-ed.
        StorePathSet closureRoots;
        for (auto & i : installables) {
            try {
                auto installableOutPath =
                    Installable::toStorePath(getEvalStore(), store, Realise::Derivation, OperateOn::Output, i);
                if (store->isValidPath(installableOutPath)) {
                    closureRoots.insert(installableOutPath);
                }
            } catch (MissingRealisation &) {
            }
        }
        if (closureRoots.empty())
            throw UsageError(
                "provided installables do not evaluate to valid store paths (perhaps they're not built yet)");
        store->computeFSClosure(closureRoots, options.pathsToDelete);
        GCResults results;
        Finally printer([&] { printFreed(false, results); });
        gcStore.collectGarbage(options, results);
    }
};

static auto rCmdStoreGCClosure = registerCommand2<CmdStoreGCClosure>({"store", "gc-closure"});
