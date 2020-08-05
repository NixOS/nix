// FIXME: integrate this with nix path-info?

#include <iomanip>
#include <nlohmann/json.hpp>

#include "command.hh"
#include "common-args.hh"
#include "store-api.hh"
#include "archive.hh"
#include "derivations.hh"


using namespace nix;

struct CmdShowDerivation : InstallablesCommand
{
    bool recursive = false;

    CmdShowDerivation()
    {
        addFlag({
            .longName = "recursive",
            .shortName = 'r',
            .description = "include the dependencies of the specified derivations",
            .handler = {&recursive, true}
        });
    }

    std::string description() override
    {
        return "show the contents of a store derivation";
    }

    Examples examples() override
    {
        return {
            Example{
                "To show the store derivation that results from evaluating the Hello package:",
                "nix show-derivation nixpkgs#hello"
            },
            Example{
                "To show the full derivation graph (if available) that produced your NixOS system:",
                "nix show-derivation -r /run/current-system"
            },
        };
    }

    Category category() override { return catUtility; }

    void run(ref<Store> store) override
    {
        auto drvPaths = toDerivations(store, installables, true);

        if (recursive) {
            StorePathSet closure;
            store->computeFSClosure(drvPaths, closure);
            drvPaths = std::move(closure);
        }

        nlohmann::json jsonRoot;

        for (auto & drvPath : drvPaths) {
            if (!drvPath.isDerivation()) continue;

            auto & drvObj = jsonRoot[store->printStorePath(drvPath)];

            auto drv = store->readDerivation(drvPath);

            {
                auto & outputsObj= drvObj["outputs"];
                for (auto & output : drv.outputs) {
                    auto & outputObj = outputsObj[output.first];
                    outputObj["path"] = store->printStorePath(output.second.path(*store, drv.name));
                    if (auto hash = std::get_if<DerivationOutputFixed>(&output.second.output)) {
                        outputObj["hashAlgo"] = hash->hash.printMethodAlgo();
                        outputObj["hash"] = hash->hash.hash.to_string(Base16, false);
                    }
                }
            }

            {
                auto & inputsList = drvObj["inputSrcs"];
                for (auto & input : drv.inputSrcs)
                    inputsList.push_back(store->printStorePath(input));
            }

            {
                auto & inputDrvsObj = drvObj["inputDrvs"];
                for (auto & input : drv.inputDrvs) {
                    auto & inputList = inputDrvsObj[store->printStorePath(input.first)];
                    for (auto & outputId : input.second)
                        inputList.push_back(outputId);
                }
            }

            drvObj["platform"] = drv.platform;
            drvObj["builder"] = drv.builder;

            {
                auto & argsList = drvObj["args"];
                for (auto & arg : drv.args)
                    argsList.push_back(arg);
            }

            {
                auto & envObj= drvObj["env"];
                for (auto & var : drv.env)
                    envObj[var.first] = var.second;
            }
        }

        std::cout << std::setw(4) << jsonRoot << std::endl;
    }
};

static auto r1 = registerCommand<CmdShowDerivation>("show-derivation");
