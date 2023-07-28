// FIXME: rename to 'nix plan add' or 'nix derivation add'?

#include "command.hh"
#include "common-args.hh"
#include "store-api.hh"
#include "topo-sort.hh"
#include "archive.hh"
#include "derivations.hh"
#include <nlohmann/json.hpp>

using namespace nix;
using json = nlohmann::json;

struct CmdAddDerivation : MixDryRun, StoreCommand
{
    std::string description() override
    {
        return "Add derivations to the store";
    }

    std::string doc() override
    {
        return
          #include "derivation-add.md"
          ;
    }

    enum InputType : bool { InputDrv = true, InputSrc = false };
    using MissingInputs = std::vector<std::tuple<std::string_view, InputType, std::string_view>>;
    using DerivationsToAdd = std::map<StorePath, Derivation>;

    Category category() override { return catUtility; }


    Error makeMissingInputsError(const MissingInputs & missingInputs) {
        std::ostringstream missingInputsMsg;
        missingInputsMsg << "Missing inputs:\n";

        bool missingSources = false;
        bool missingDerivations = false;
        for (auto& [drv, inputType, missingInputPath] : missingInputs) {
            if (inputType == InputSrc)
                missingSources = true;
            if (inputType == InputDrv)
                missingDerivations = true;

            missingInputsMsg << hintfmt(
                "'%s' requires '%s', but it is %s\n",
                drv,
                missingInputPath,
                inputType == InputSrc
                    ? "not present in the Nix Store"
                    : "not in the input JSON or the Nix Store");
        }
        logError(Error(missingInputsMsg.str()).info());

        std::ostringstream explanation;
        explanation << "Some inputs are missing, so the derivations can't be added.\n";
        if (missingSources) {
            explanation << "- 'nix derivation add' can only add derivations, not sources.\n"
                "  To easily transfer multiple sources from one store to another, use 'nix copy'.\n";
        }
        if (missingDerivations) {
            explanation << "- All required derivations must be in the store or the JSON input.\n"
                "  You may want to re-export the JSON with 'nix derivation show -r'.\n";
        }
        return Error(explanation.str());
    }

    void tryToSubstituteInputs(const ref<Store> store, const DerivationsToAdd & derivationsToAdd) {
        StorePathSet requiredInputs;
        for (auto& [_storePath, drv] : derivationsToAdd) {
            for (auto& [inputPath, _] : drv.inputDrvs) {
                requiredInputs.insert(inputPath);
            }
            for (auto& inputPath : drv.inputSrcs) {
                requiredInputs.insert(inputPath);
            }
        }
        store->queryValidPaths(requiredInputs, Substitute);
    }

    void addSingleDerivation(
        const ref<Store> store,
        const Derivation& drv,
        const std::optional<StorePath> & expectedPath)
    {
        auto drvPath = writeDerivation(*store, drv, NoRepair, true);

        if (expectedPath.has_value() && expectedPath.value() != drvPath) {
            throw Error(
                "Derivation was named '%s' in the input file, but its actual path is '%s'",
                store->printStorePath(expectedPath.value()),
                store->printStorePath(drvPath));
        }
        drv.checkInvariants(*store, drvPath);

        writeDerivation(*store, drv, NoRepair, dryRun);

        logger->cout("%s", store->printStorePath(drvPath));
    }

    void run(ref<Store> store) override
    {
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(drainFD(STDIN_FILENO));
        } catch (nlohmann::json::exception & e) {
            throw Error(
                "Parsing JSON input failed with code '%s': %s",
                e.id,
                e.what()
            );
        }

        /* Handle the special case where a single unwrapped derivation is received */
        if (!json.value("name", "").empty()) {
            debug("Input has 'name' attribute. Will assume it's a single derivation.");
            try {
                auto drv = Derivation::fromJSON(*store, json);
                addSingleDerivation(store, drv, std::optional<StorePath>());
            } catch (Error & e) {
                e.addTrace({}, "while adding single anonymous JSON derivation");
                throw;
            }
            return;
        }

        /* Read all derivations from the input */
        std::map<StorePath, Derivation> derivationsToAdd;
        for (auto& [rawStorePath, jsonDrv] : json.items()) {
            try {
                auto storePath = store->parseStorePath(rawStorePath);
                auto drv = Derivation::fromJSON(*store, jsonDrv);
                derivationsToAdd[storePath] = std::move(drv);
            } catch (Error & e) {
                e.addTrace({}, "while reading JSON derivation with key '%s'", rawStorePath);
                throw;
            }
        }

        /* Try substituting the inputs, this might help in some situations */
        tryToSubstituteInputs(store, derivationsToAdd);

        /* Ensure all inputSrcs are valid and all inputDrvs are valid or will be added.
           If that isn't the case, adding the derivations would fail.
           Checking now allows for more comprehensible error messages */
        MissingInputs missingInputs;
        for (auto & [storePath, drv] : derivationsToAdd) {
            for (auto & [inputPath, _] : drv.inputDrvs) {
                if (!store->isValidPath(inputPath) && !derivationsToAdd.contains(inputPath)) {
                    missingInputs.push_back({storePath.to_string(), InputDrv, inputPath.to_string()});
                }
            }
            for (auto & inputPath : drv.inputSrcs) {
                if (!store->isValidPath(inputPath)) {
                    missingInputs.push_back({storePath.to_string(), InputSrc, inputPath.to_string()});
                }
            }
        }
        if (!missingInputs.empty()) {
            throw makeMissingInputsError(missingInputs);
        }

        /* Derivations can only be added if all their inputs are valid. Sort them
           into reverse depedency order so they can all be added in one pass. */
        std::set<StorePath> storePathsToOrder;
        for (auto & [storePath, _] : derivationsToAdd) {
            storePathsToOrder.insert(storePath);
        }

        auto addDrvsListOrdered = topoSort(
            storePathsToOrder,
            {[&](const StorePath & drvStorePath){
                auto derivation = derivationsToAdd[drvStorePath];
                std::set<StorePath> dependencies{};
                for (auto & [depStorePath, _] : derivation.inputDrvs ) {
                    dependencies.insert(depStorePath);
                }
                return dependencies;
            }},
            {[&](const StorePath & path, const StorePath & parent) {
                return Error(
                    "Cicular depedency in JSON input: '%s' has direct depedency on '%s', but the latter is further up the dependency tree",
                    path.to_string(),
                    parent.to_string());
            }}
        );
        std::reverse(addDrvsListOrdered.begin(), addDrvsListOrdered.end());

        /* Finally, add all the derivations */
        for (auto & storePath : addDrvsListOrdered) {
            auto drv = derivationsToAdd[storePath];
            try {
                addSingleDerivation(store, drv, storePath);
            } catch (Error & e ) {
                e.addTrace({}, "while trying to add derivation '%s'", storePath.to_string());
                throw;
            }
        }
    }
};

static auto rCmdAddDerivation = registerCommand2<CmdAddDerivation>({"derivation", "add"});
