#include "eval.hh"
#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "local-fs-store.hh"

#include <nlohmann/json.hpp>

using namespace nix;

struct CmdBuild : InstallablesCommand, MixDryRun, MixJSON, MixProfile
{
    Path outLink = "result";
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
        auto buildables = build(store, dryRun ? Realise::Nothing : Realise::Outputs, installables, buildMode);

        if (dryRun) return;

        if (outLink != "")
            if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>())
                for (const auto & [_i, buildable] : enumerate(buildables)) {
                    auto i = _i;
                    std::visit(overloaded {
                        [&](DerivedPathWithHints::Opaque bo) {
                            std::string symlink = outLink;
                            if (i) symlink += fmt("-%d", i);
                            store2->addPermRoot(bo.path, absPath(symlink));
                        },
                        [&](DerivedPathWithHints::Built bfd) {
                            auto builtOutputs = resolveDerivedPathWithHints(*store, bfd);
                            for (auto & output : builtOutputs) {
                                std::string symlink = outLink;
                                if (i) symlink += fmt("-%d", i);
                                if (output.first != "out") symlink += fmt("-%s", output.first);
                                store2->addPermRoot(output.second, absPath(symlink));
                            }
                        },
                    }, buildable.raw());
                }

        updateProfile(buildables);

        if (json) logger->cout("%s", derivedPathsWithHintsToJSON(buildables, *store).dump());
    }
};

static auto rCmdBuild = registerCommand<CmdBuild>("build");
