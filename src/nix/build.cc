#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/store/local-fs-store.hh"

#include <nlohmann/json.hpp>

using namespace nix;

/* This serialization code is diferent from the canonical (single)
   derived path serialization because:

   - It looks up output paths where possible

   - It includes the store dir in store paths

   We might want to replace it with the canonical format at some point,
   but that would be a breaking change (to a still-experimental but
   widely-used command, so that isn't being done at this time just yet.
 */

static nlohmann::json toJSON(Store & store, const SingleDerivedPath::Opaque & o)
{
    return store.printStorePath(o.path);
}

static nlohmann::json toJSON(Store & store, const SingleDerivedPath & sdp);
static nlohmann::json toJSON(Store & store, const DerivedPath & dp);

static nlohmann::json toJSON(Store & store, const SingleDerivedPath::Built & sdpb)
{
    nlohmann::json res;
    res["drvPath"] = toJSON(store, *sdpb.drvPath);
    // Fallback for the input-addressed derivation case: We expect to always be
    // able to print the output paths, so let’s do it
    // FIXME try-resolve on drvPath
    const auto outputMap = store.queryPartialDerivationOutputMap(resolveDerivedPath(store, *sdpb.drvPath));
    res["output"] = sdpb.output;
    auto outputPathIter = outputMap.find(sdpb.output);
    if (outputPathIter == outputMap.end())
        res["outputPath"] = nullptr;
    else if (std::optional p = outputPathIter->second)
        res["outputPath"] = store.printStorePath(*p);
    else
        res["outputPath"] = nullptr;
    return res;
}

static nlohmann::json toJSON(Store & store, const DerivedPath::Built & dpb)
{
    nlohmann::json res;
    res["drvPath"] = toJSON(store, *dpb.drvPath);
    // Fallback for the input-addressed derivation case: We expect to always be
    // able to print the output paths, so let’s do it
    // FIXME try-resolve on drvPath
    const auto outputMap = store.queryPartialDerivationOutputMap(resolveDerivedPath(store, *dpb.drvPath));
    for (const auto & [output, outputPathOpt] : outputMap) {
        if (!dpb.outputs.contains(output))
            continue;
        if (outputPathOpt)
            res["outputs"][output] = store.printStorePath(*outputPathOpt);
        else
            res["outputs"][output] = nullptr;
    }
    return res;
}

static nlohmann::json toJSON(Store & store, const SingleDerivedPath & sdp)
{
    return std::visit([&](const auto & buildable) { return toJSON(store, buildable); }, sdp.raw());
}

static nlohmann::json toJSON(Store & store, const DerivedPath & dp)
{
    return std::visit([&](const auto & buildable) { return toJSON(store, buildable); }, dp.raw());
}

static nlohmann::json derivedPathsToJSON(const DerivedPaths & paths, Store & store)
{
    auto res = nlohmann::json::array();
    for (auto & t : paths) {
        res.push_back(toJSON(store, t));
    }
    return res;
}

static nlohmann::json
builtPathsWithResultToJSON(const std::vector<BuiltPathWithResult> & buildables, const Store & store)
{
    auto res = nlohmann::json::array();
    for (auto & b : buildables) {
        auto j = b.path.toJSON(store);
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
