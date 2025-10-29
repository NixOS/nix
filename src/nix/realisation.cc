#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"

#include <nlohmann/json.hpp>

using namespace nix;

struct CmdRealisation : NixMultiCommand
{
    CmdRealisation()
        : NixMultiCommand("realisation", RegisterCommand::getCommandsFor({"realisation"}))
    {
    }

    std::string description() override
    {
        return "manipulate a Nix realisation";
    }

    Category category() override
    {
        return catUtility;
    }
};

static auto rCmdRealisation = registerCommand<CmdRealisation>("realisation");

struct CmdRealisationInfo : BuiltPathsCommand, MixJSON
{
    std::string description() override
    {
        return "query information about one or several realisations";
    }

    std::string doc() override
    {
        return
#include "realisation/info.md"
            ;
    }

    Category category() override
    {
        return catSecondary;
    }

    void run(ref<Store> store, BuiltPaths && paths, BuiltPaths && rootPaths) override
    {
        experimentalFeatureSettings.require(Xp::CaDerivations);
        RealisedPath::Set realisations;

        for (auto & builtPath : paths) {
            auto theseRealisations = builtPath.toRealisedPaths(*store);
            realisations.insert(theseRealisations.begin(), theseRealisations.end());
        }

        if (json) {
            nlohmann::json res = nlohmann::json::array();
            for (auto & path : realisations) {
                nlohmann::json currentPath;
                if (auto realisation = std::get_if<Realisation>(&path.raw))
                    currentPath = *realisation;
                else
                    currentPath["opaquePath"] = store->printStorePath(path.path());

                res.push_back(currentPath);
            }
            printJSON(res);
        } else {
            for (auto & path : realisations) {
                if (auto realisation = std::get_if<Realisation>(&path.raw)) {
                    logger->cout("%s %s", realisation->id.to_string(), store->printStorePath(realisation->outPath));
                } else
                    logger->cout("%s", store->printStorePath(path.path()));
            }
        }
    }
};

static auto rCmdRealisationInfo = registerCommand2<CmdRealisationInfo>({"realisation", "info"});
