#include "command.hh"
#include "common-args.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "local-fs-store.hh"
#include "progress-bar.hh"

#include <nlohmann/json.hpp>

using namespace nix;

static nlohmann::json storePathSetToJSON(const StorePathSet & paths, Store & store)
{
    auto res = nlohmann::json::object();
    for (auto & path : paths) {
        res[store.printStorePath(path)] = nlohmann::json::object();
    }
    return res;
}

// TODO deduplicate with other code also setting such out links.
static void
createOutLinks(const std::filesystem::path & outLink, const StorePathSet & derivations, LocalFSStore & store)
{
    for (const auto & [_i, drv] : enumerate(derivations)) {
        auto i = _i;
        auto symlink = outLink;

        if (i)
            symlink += fmt("-%d", i);
        store.addPermRoot(drv, absPath(symlink.string()));
    }
}

struct CmdDerivationInstantiate : InstallablesCommand, MixJSON
{
    Path outLink = "drv";
    bool printOutputPaths = false;

    CmdDerivationInstantiate()
    {
        addFlag(
            {.longName = "out-link",
             .shortName = 'o',
             .description = "Use *path* as prefix for the symlinks to the evaluation results. It defaults to `drv`.",
             .labels = {"path"},
             .handler = {&outLink},
             .completer = completePath});

        addFlag({
            .longName = "no-link",
            .description = "Do not create symlinks to the evaluation results.",
            .handler = {&outLink, Path("")},
        });
    }

    std::string description() override
    {
        return "Force the evaluation of the expression and return the corresponding .drv";
    }

    std::string doc() override
    {
        return
#include "derivation-instantiate.md"
            ;
    }

    Category category() override
    {
        return catSecondary;
    }

    void run(ref<Store> store, Installables && installables) override
    {
        auto drvPaths = Installable::toDerivations(store, installables, false);

        if (outLink != "")
            if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>())
                createOutLinks(outLink, drvPaths, *store2);

        if (json) {
            logger->cout("%s", storePathSetToJSON(drvPaths, *store).dump());
        } else {
            stopProgressBar();
            for (auto & path : drvPaths) {
                logger->cout(store->printStorePath(path));
            }
        }
    }
};

static auto rCmdDerivationInstantiate = registerCommand2<CmdDerivationInstantiate>({"derivation", "instantiate"});
