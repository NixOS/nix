// FIXME: integrate this with nix path-info?

#include "command.hh"
#include "common-args.hh"
#include "store-api.hh"
#include "archive.hh"
#include "json.hh"
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

        {

        JSONObject jsonRoot(std::cout, true);

        for (auto & drvPath : drvPaths) {
            if (!drvPath.isDerivation()) continue;

            auto drvObj(jsonRoot.object(store->printStorePath(drvPath)));

            auto drv = store->readDerivation(drvPath);

            {
                auto outputsObj(drvObj.object("outputs"));
                for (auto & [_outputName, output] : drv.outputs) {
                    auto & outputName = _outputName; // work around clang bug
                    auto outputObj { outputsObj.object(outputName) };
                    std::visit(overloaded {
                        [&](DerivationOutputInputAddressed doi) {
                            outputObj.attr("path", store->printStorePath(doi.path));
                        },
                        [&](DerivationOutputCAFixed dof) {
                            outputObj.attr("path", store->printStorePath(dof.path(*store, drv.name, outputName)));
                            outputObj.attr("hashAlgo", dof.hash.printMethodAlgo());
                            outputObj.attr("hash", dof.hash.hash.to_string(Base16, false));
                        },
                        [&](DerivationOutputCAFloating dof) {
                            outputObj.attr("hashAlgo", makeFileIngestionPrefix(dof.method) + printHashType(dof.hashType));
                        },
                    }, output.output);
                }
            }

            {
                auto inputsList(drvObj.list("inputSrcs"));
                for (auto & input : drv.inputSrcs)
                    inputsList.elem(store->printStorePath(input));
            }

            {
                auto inputDrvsObj(drvObj.object("inputDrvs"));
                for (auto & input : drv.inputDrvs) {
                    auto inputList(inputDrvsObj.list(store->printStorePath(input.first)));
                    for (auto & outputId : input.second)
                        inputList.elem(outputId);
                }
            }

            drvObj.attr("platform", drv.platform);
            drvObj.attr("builder", drv.builder);

            {
                auto argsList(drvObj.list("args"));
                for (auto & arg : drv.args)
                    argsList.elem(arg);
            }

            {
                auto envObj(drvObj.object("env"));
                for (auto & var : drv.env)
                    envObj.attr(var.first, var.second);
            }
        }

        }

        std::cout << "\n";
    }
};

static auto rCmdShowDerivation = registerCommand<CmdShowDerivation>("show-derivation");
