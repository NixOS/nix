// nix derivation source-origins — map inputSrc store paths back to
// the original filesystem source paths that were copied into the store
// during evaluation.  This is the missing link between `nix derivation
// show` (which only knows about store paths) and the working-tree
// locations that produced them.

#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/store/store-api.hh"
#include "nix/store/derivations.hh"
#include "nix/expr/eval.hh"
#include <nlohmann/json.hpp>

using namespace nix;
using json = nlohmann::json;

struct CmdDerivationSourceOrigins : InstallablesCommand, MixPrintJSON
{
    bool recursive = false;

    CmdDerivationSourceOrigins()
    {
        addFlag({
            .longName = "recursive",
            .shortName = 'r',
            .description = "Include the dependencies of the specified derivations.",
            .handler = {&recursive, true},
        });
    }

    std::string description() override
    {
        return "map derivation inputSrcs back to their original source paths";
    }

    std::string doc() override
    {
        return
#include "derivation-source-origins.md"
            ;
    }

    Category category() override
    {
        return catUtility;
    }

    void run(ref<Store> store, Installables && installables) override
    {
        // Step 1: Evaluate installables to derivation paths.
        // This triggers copyPathToStore for every source reference in the
        // Nix expressions, populating EvalState::storeToSrc.
        auto drvPaths = Installable::toDerivations(store, installables, true);

        if (recursive) {
            StorePathSet closure;
            store->computeFSClosure(drvPaths, closure);
            drvPaths = std::move(closure);
        }

        // Step 2: Grab the full store→source mapping that was built during
        // evaluation.
        auto state = getEvalState();
        auto origins = state->getSourceOrigins();

        // Step 3: For each derivation, read its inputSrcs and look up
        // source origins.
        json jsonRoot = json::object();

        for (auto & drvPath : drvPaths) {
            if (!drvPath.isDerivation())
                continue;

            auto drv = store->readDerivation(drvPath);

            json inputSrcsJson = json::object();
            for (auto & inputSrc : drv.inputSrcs) {
                json entry = json::object();
                entry["storePath"] = store->printStorePath(inputSrc);

                // Look up the original source path from our reverse mapping.
                auto it = origins.find(inputSrc);
                if (it != origins.end()) {
                    entry["sourcePath"] = it->second.to_string();
                } else {
                    entry["sourcePath"] = nullptr;
                }

                inputSrcsJson[store->printStorePath(inputSrc)] = entry;
            }

            json drvJson = json::object();
            drvJson["drvPath"] = store->printStorePath(drvPath);
            drvJson["name"] = drv.name;
            drvJson["inputSrcs"] = inputSrcsJson;
            jsonRoot[store->printStorePath(drvPath)] = drvJson;
        }

        printJSON(jsonRoot);
    }
};

static auto rCmdDerivationSourceOrigins =
    registerCommand2<CmdDerivationSourceOrigins>({"derivation", "source-origins"});
