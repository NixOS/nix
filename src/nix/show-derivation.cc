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

            json& drvObj = jsonRoot[store->printStorePath(drvPath)];

            auto drv = store->readDerivation(drvPath);

            {
                json& outputsObj = drvObj["outputs"];
                outputsObj = json::object();
                for (auto & [_outputName, output] : drv.outputs) {
                    auto & outputName = _outputName; // work around clang bug
                    auto& outputObj = outputsObj[outputName];
                    outputObj = json::object();
                    std::visit(overloaded {
                        [&](const DerivationOutput::InputAddressed & doi) {
                            outputObj["path"] = store->printStorePath(doi.path);
                        },
                        [&](const DerivationOutput::CAFixed & dof) {
                            outputObj["path"] = store->printStorePath(dof.path(*store, drv.name, outputName));
                            outputObj["hashAlgo"] = dof.hash.printMethodAlgo();
                            outputObj["hash"] = dof.hash.hash.to_string(Base16, false);
                        },
                        [&](const DerivationOutput::CAFloating & dof) {
                            outputObj["hashAlgo"] = makeFileIngestionPrefix(dof.method) + printHashType(dof.hashType);
                        },
                        [&](const DerivationOutput::Deferred &) {},
                        [&](const DerivationOutput::Impure & doi) {
                            outputObj["hashAlgo"] = makeFileIngestionPrefix(doi.method) + printHashType(doi.hashType);
                            outputObj["impure"] = true;
                        },
                    }, output.raw());
                }
            }

            {
                auto& inputsList = drvObj["inputSrcs"];
                inputsList = json::array();
                for (auto & input : drv.inputSrcs)
                    inputsList.emplace_back(store->printStorePath(input));
            }

            {
                auto& inputDrvsObj = drvObj["inputDrvs"];
                inputDrvsObj = json::object();
                for (auto & input : drv.inputDrvs)
                    inputDrvsObj[store->printStorePath(input.first)] = input.second;
            }

            drvObj["system"] = drv.platform;
            drvObj["builder"] = drv.builder;
            drvObj["args"] = drv.args;
            drvObj["env"] = drv.env;
        }
        std::cout << jsonRoot.dump(2) << std::endl;
    }
};

static auto rCmdShowDerivation = registerCommand<CmdShowDerivation>("show-derivation");
