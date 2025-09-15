#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/store/local-fs-store.hh"

#include <nlohmann/json.hpp>

using namespace nix;

static nlohmann::json derivedPathsToJSON(const DerivedPaths & paths, Store & store)
{
    auto res = nlohmann::json::array();
    for (auto & t : paths) {
        std::visit([&](const auto & t) { res.push_back(t.toJSON(store)); }, t.raw());
    }
    return res;
}

static nlohmann::json
builtPathsWithResultToJSON(const std::vector<BuiltPathWithResult> & buildables, const Store & store)
{
    auto res = nlohmann::json::array();
    for (auto & b : buildables) {
        std::visit(
            [&](const auto & t) {
                auto j = t.toJSON(store);
                if (b.result) {
                    if (b.result->startTime)
                        j["startTime"] = b.result->startTime;
                    if (b.result->stopTime)
                        j["stopTime"] = b.result->stopTime;
                    if (b.result->cpuUser)
                        j["cpuUser"] = ((double) b.result->cpuUser->count()) / 1000000;
                    if (b.result->cpuSystem)
                        j["cpuSystem"] = ((double) b.result->cpuSystem->count()) / 1000000;
                }
                res.push_back(j);
            },
            b.path.raw());
    }
    return res;
}

struct CmdBuild : InstallablesCommand, MixOutLinkByDefault, MixDryRun, MixJSON, MixProfile
{
    bool printOutputPaths = false;
    BuildMode buildMode = bmNormal;

    CmdBuild()
    {
        addFlag({
            .longName = "print-out-paths",
            .description = "Print the resulting output paths",
            .handler = {&printOutputPaths, true},
        });

        addFlag({
            .longName = "rebuild",
            .description = "Rebuild an already built package and compare the result to the existing store paths.",
            .handler = {&buildMode, bmCheck},
        });
    }

    std::string description() override
    {
        return "build a derivation or fetch a store path";
    }

    std::string doc() override
    {
        return
#include "build.md"
            ;
    }

    void run(ref<Store> store, Installables && installables) override
    {
        if (dryRun) {
            std::vector<DerivedPath> pathsToBuild;

            for (auto & i : installables)
                for (auto & b : i->toDerivedPaths())
                    pathsToBuild.push_back(b.path);

            printMissing(store, pathsToBuild, lvlError);

            if (json)
                printJSON(derivedPathsToJSON(pathsToBuild, *store));

            return;
        }

        auto buildables =
            Installable::build(getEvalStore(), store, Realise::Outputs, installables, repair ? bmRepair : buildMode);

        if (json)
            logger->cout("%s", builtPathsWithResultToJSON(buildables, *store).dump());

        createOutLinksMaybe(buildables, store);

        if (printOutputPaths) {
            logger->stop();
            for (auto & buildable : buildables) {
                std::visit(
                    overloaded{
                        [&](const BuiltPath::Opaque & bo) { logger->cout(store->printStorePath(bo.path)); },
                        [&](const BuiltPath::Built & bfd) {
                            for (auto & output : bfd.outputs) {
                                logger->cout(store->printStorePath(output.second));
                            }
                        },
                    },
                    buildable.path.raw());
            }
        }

        BuiltPaths buildables2;
        for (auto & b : buildables)
            buildables2.push_back(b.path);
        updateProfile(buildables2);
    }
};

static auto rCmdBuild = registerCommand<CmdBuild>("build");
