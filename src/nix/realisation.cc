#include "command.hh"
#include "common-args.hh"

#include <nlohmann/json.hpp>

using namespace nix;

struct CmdRealisation : virtual NixMultiCommand
{
    CmdRealisation() : MultiCommand(RegisterCommand::getCommandsFor({"realisation"}))
    { }

    std::string description() override
    {
        return "manipulate a Nix realisation";
    }

    Category category() override { return catUtility; }

    void run() override
    {
        if (!command)
            throw UsageError("'nix realisation' requires a sub-command.");
        command->second->prepare();
        command->second->run();
    }
};

static auto rCmdRealisation = registerCommand<CmdRealisation>("realisation");

struct CmdRealisationInfo : RealisedPathsCommand, MixJSON
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

    Category category() override { return catSecondary; }

    void run(ref<Store> store, std::vector<RealisedPath> paths) override
    {
        settings.requireExperimentalFeature("ca-derivations");
        if (json) {
            nlohmann::json res = nlohmann::json::array();
            for (auto & path : paths) {
                nlohmann::json currentPath;
                if (auto realisation = std::get_if<Realisation>(&path.raw))
                    currentPath = realisation->toJSON();
                else
                    currentPath["opaquePath"] = store->printStorePath(path.path());

                res.push_back(currentPath);
            }
            std::cout << res.dump();
        }
        else {
            for (auto & path : paths) {
                if (auto realisation = std::get_if<Realisation>(&path.raw)) {
                    std::cout <<
                        realisation->id.to_string() << " " <<
                        store->printStorePath(realisation->outPath);
                } else
                    std::cout << store->printStorePath(path.path());

                std::cout << std::endl;
            }
        }
    }
};

static auto rCmdRealisationInfo = registerCommand2<CmdRealisationInfo>({"realisation", "info"});
