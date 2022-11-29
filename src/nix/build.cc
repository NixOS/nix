#include "eval.hh"
#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "local-fs-store.hh"
#include "progress-bar.hh"

#include <nlohmann/json.hpp>

using namespace nix;

nlohmann::json derivedPathsToJSON(const DerivedPaths & paths, ref<Store> store)
{
    auto res = nlohmann::json::array();
    for (auto & t : paths) {
        std::visit([&res, store](const auto & t) {
            res.push_back(t.toJSON(store));
        }, t.raw());
    }
    return res;
}

nlohmann::json builtPathsWithResultToJSON(const std::vector<BuiltPathWithResult> & buildables, ref<Store> store)
{
    auto res = nlohmann::json::array();
    for (auto & b : buildables) {
        std::visit([&](const auto & t) {
            auto j = t.toJSON(store);
            if (b.result) {
                j["startTime"] = b.result->startTime;
                j["stopTime"] = b.result->stopTime;
                if (b.result->cpuUser)
                    j["cpuUser"] = ((double) b.result->cpuUser->count()) / 1000000;
                if (b.result->cpuSystem)
                    j["cpuSystem"] = ((double) b.result->cpuSystem->count()) / 1000000;
            }
            res.push_back(j);
        }, b.path.raw());
    }
    return res;
}

struct CmdBuild : InstallablesCommand, MixDryRun, MixJSON, MixProfile
{
    Path outLink = "result";
    bool printOutputPaths = false;
    BuildMode buildMode = bmNormal;

    CmdBuild()
    {
        addFlag({
            .longName = "out-link",
            .shortName = 'o',
            .description = "Use *path* as prefix for the symlinks to the build results. It defaults to `result`.",
            .labels = {"path"},
            .handler = {&outLink},
            .completer = completePath
        });

        addFlag({
            .longName = "no-link",
            .description = "Do not create symlinks to the build results.",
            .handler = {&outLink, Path("")},
        });

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

    void run(ref<Store> store) override
    {
        if (dryRun) {
            std::vector<DerivedPath> pathsToBuild;

            for (auto & i : installables) {
                auto b = i->toDerivedPaths();
                pathsToBuild.insert(pathsToBuild.end(), b.begin(), b.end());
            }
            printMissing(store, pathsToBuild, lvlError);
            if (json)
                logger->cout("%s", derivedPathsToJSON(pathsToBuild, store).dump());
            return;
        }

        auto buildables = Installable::build(
            getEvalStore(), store,
            Realise::Outputs,
            installables, buildMode);

        if (json) logger->cout("%s", builtPathsWithResultToJSON(buildables, store).dump());

        if (outLink != "")
            if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>())
                for (const auto & [_i, buildable] : enumerate(buildables)) {
                    auto i = _i;
                    std::visit(overloaded {
                        [&](const BuiltPath::Opaque & bo) {
                            std::string symlink = outLink;
                            if (i) symlink += fmt("-%d", i);
                            store2->addPermRoot(bo.path, absPath(symlink));
                        },
                        [&](const BuiltPath::Built & bfd) {
                            for (auto & output : bfd.outputs) {
                                std::string symlink = outLink;
                                if (i) symlink += fmt("-%d", i);
                                if (output.first != "out") symlink += fmt("-%s", output.first);
                                store2->addPermRoot(output.second, absPath(symlink));
                            }
                        },
                    }, buildable.path.raw());
                }

        if (printOutputPaths) {
            stopProgressBar();
            for (auto & buildable : buildables) {
                std::visit(overloaded {
                    [&](const BuiltPath::Opaque & bo) {
                        std::cout << store->printStorePath(bo.path) << std::endl;
                    },
                    [&](const BuiltPath::Built & bfd) {
                        for (auto & output : bfd.outputs) {
                            std::cout << store->printStorePath(output.second) << std::endl;
                        }
                    },
                }, buildable.path.raw());
            }
        }

        BuiltPaths buildables2;
        for (auto & b : buildables)
            buildables2.push_back(b.path);
        updateProfile(buildables2);
    }
};

static auto rCmdBuild = registerCommand<CmdBuild>("build");
