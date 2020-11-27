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
            .description = "path of the symlink to the build result",
            .labels = {"path"},
            .handler = {&outLink},
            .completer = completePath
        });

        addFlag({
            .longName = "no-link",
            .description = "do not create a symlink to the build result",
            .handler = {&outLink, Path("")},
        });

        addFlag({
            .longName = "rebuild",
            .description = "rebuild an already built package and compare the result to the existing store paths",
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

        PathSet symlinks;

        if (outLink != "")
            if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>())
                for (size_t i = 0; i < buildables.size(); ++i)
                    std::visit(overloaded {
                        [&](BuildableOpaque bo) {
                            std::string symlink = outLink;
                            if (i) symlink += fmt("-%d", i);
                            symlink = absPath(symlink);
                            store2->addPermRoot(bo.path, symlink);
                            symlinks.insert(symlink);
                        },
                        [&](BuildableFromDrv bfd) {
                            auto builtOutputs = store->queryDerivationOutputMap(bfd.drvPath);
                            for (auto & output : builtOutputs) {
                                std::string symlink = outLink;
                                if (i) symlink += fmt("-%d", i);
                                if (output.first != "out") symlink += fmt("-%s", output.first);
                                symlink = absPath(symlink);
                                store2->addPermRoot(output.second, symlink);
                                symlinks.insert(symlink);
                            }
                        },
                    }, buildables[i]);

        updateProfile(buildables);

        if (json)
            logger->cout("%s", buildablesToJSON(buildables, store).dump());
        else
            notice(
                ANSI_GREEN "Build succeeded." ANSI_NORMAL
                " The result is available through the symlink " ANSI_BOLD "%s" ANSI_NORMAL ".",
                showPaths(symlinks));
    }
};

static auto rCmdBuild = registerCommand<CmdBuild>("build");
