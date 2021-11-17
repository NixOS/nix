// FIXME: integrate this with nix path-info?
// FIXME: rename to 'nix store show-derivation' or 'nix debug show-derivation'?

#include "command.hh"
#include "common-args.hh"
#include "store-api.hh"
#include "archive.hh"
#include "derivations.hh"
#include <nlohmann/json.hpp>

using namespace nix;
using json = nlohmann::json;

struct CmdShowDerivation : InstallablesCommand
{
    bool recursive = false;

    CmdShowDerivation()
    {
        addFlag({
            .longName = "recursive",
            .shortName = 'r',
            .description = "Include the dependencies of the specified derivations.",
            .handler = {&recursive, true}
        });
    }

    std::string description() override
    {
        return "show the contents of a store derivation";
    }

    std::string doc() override
    {
        return
          #include "show-derivation.md"
          ;
    }

    Category category() override { return catUtility; }

    void run(ref<Store> store) override
    {
        auto drvPaths = Installable::toDerivations(store, installables, true);

        if (recursive) {
            StorePathSet closure;
            store->computeFSClosure(drvPaths, closure);
            drvPaths = std::move(closure);
        }

        json jsonRoot = json::object();

        for (auto & drvPath : drvPaths) {
            if (!drvPath.isDerivation()) continue;

            jsonRoot[store->printStorePath(drvPath)] =
                store->readDerivation(drvPath).toJSON(*store);
        }
        std::cout << jsonRoot.dump(2) << std::endl;
    }
};

static auto rCmdShowDerivation = registerCommand<CmdShowDerivation>("show-derivation");
