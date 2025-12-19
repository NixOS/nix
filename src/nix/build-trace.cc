#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/store/gc-store.hh"
#include "nix/store/store-cast.hh"

#include <nlohmann/json.hpp>
#include <ranges>

namespace nix {

struct CmdBuildTrace : NixMultiCommand
{
    CmdBuildTrace()
        : NixMultiCommand("build-trace", RegisterCommand::getCommandsFor({"store", "build-trace"}))
    {
    }

    std::string description() override
    {
        return "manipulate a Nix build trace";
    }

    Category category() override
    {
        return catUtility;
    }

    std::optional<ExperimentalFeature> experimentalFeature() override
    {
        return Xp::CaDerivations;
    }
};

static auto rCmdBuildTrace = registerCommand2<CmdBuildTrace>({"store", "build-trace"});

struct CmdBuildTraceInfo : BuiltPathsCommand, MixJSON
{
    std::string description() override
    {
        return "query information about one or several build traces";
    }

    std::string doc() override
    {
        return
#include "build-trace/info.md"
            ;
    }

    Category category() override
    {
        return catSecondary;
    }

    void run(ref<Store> store, BuiltPaths && paths, BuiltPaths && rootPaths) override
    {
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

static auto rCmdBuildTraceInfo = registerCommand2<CmdBuildTraceInfo>({"store", "build-trace", "info"});

struct CmdBuildTraceDelete : virtual StoreCommand
{
    std::vector<std::string> ids;

    CmdBuildTraceDelete()
    {
        expectArgs({
            .label = "id",
            .handler = {&ids},
        });
    }

    std::string description() override
    {
        return "delete build traces from the store";
    }

    std::string doc() override
    {
        return
#include "build-trace/delete.md"
            ;
    }

    Category category() override
    {
        return catSecondary;
    }

    void run(ref<Store> store) override
    {
        auto & gcStore = require<GcStore>(*store);

        auto keys = ids | std::views::transform([&](std::string_view s) { return DrvOutput::parse(*store, s); })
                    | std::ranges::to<std::set>();

        gcStore.deleteBuildTraces(keys);
    }
};

static auto rCmdBuildTraceDelete = registerCommand2<CmdBuildTraceDelete>({"store", "build-trace", "delete"});

} // namespace nix
