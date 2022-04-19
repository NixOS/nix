// FIXME: integrate this with nix path-info?
// FIXME: rename to 'nix store show-derivation' or 'nix debug show-derivation'?

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
                        [&](const DerivationOutput::InputAddressed & doi) {
                            outputObj.attr("path", store->printStorePath(doi.path));
                        },
                        [&](const DerivationOutput::CAFixed & dof) {
                            outputObj.attr("path", store->printStorePath(dof.path(*store, drv.name, outputName)));
                            outputObj.attr("hashAlgo", printMethodAlgo(dof.ca));
                            outputObj.attr("hash", getContentAddressHash(dof.ca).to_string(Base16, false));
                            // FIXME print refs?
                        },
                        [&](const DerivationOutput::CAFloating & dof) {
                            outputObj.attr("hashAlgo", makeContentAddressingPrefix(dof.method) + printHashType(dof.hashType));
                        },
                        [&](const DerivationOutput::Deferred &) {},
                        [&](const DerivationOutput::Impure & doi) {
                            outputObj.attr("hashAlgo", makeContentAddressingPrefix(doi.method) + printHashType(doi.hashType));
                            outputObj.attr("impure", true);
                        },
                    }, output.raw());
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

            drvObj.attr("system", drv.platform);
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
