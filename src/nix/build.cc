#include "eval.hh"
#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"

using namespace nix;

struct CmdBuild : InstallablesCommand, MixDryRun, MixProfile
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

    Examples examples() override
    {
        return {
            Example{
                "To build and run GNU Hello from NixOS 17.03:",
                "nix build -f channel:nixos-17.03 hello; ./result/bin/hello"
            },
            Example{
                "To build the build.x86_64-linux attribute from release.nix:",
                "nix build -f release.nix build.x86_64-linux"
            },
            Example{
                "To make a profile point at GNU Hello:",
                "nix build --profile /tmp/profile nixpkgs#hello"
            },
        };
    }

    void run(ref<Store> store) override
    {
        auto buildables = build(store, dryRun ? Realise::Nothing : Realise::Outputs, installables, buildMode);

        if (dryRun) return;

        if (outLink != "")
            if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>())
                for (size_t i = 0; i < buildables.size(); ++i)
                    std::visit(overloaded {
                        [&](BuildableOpaque bo) {
                            std::string symlink = outLink;
                            if (i) symlink += fmt("-%d", i);
                            store2->addPermRoot(bo.path, absPath(symlink));
                        },
                        [&](BuildableFromDrv bfd) {
                            auto builtOutputs = store->queryDerivationOutputMap(bfd.drvPath);
                            for (auto & output : builtOutputs) {
                                std::string symlink = outLink;
                                if (i) symlink += fmt("-%d", i);
                                if (output.first != "out") symlink += fmt("-%s", output.first);
                                store2->addPermRoot(output.second, absPath(symlink));
                            }
                        },
                    }, buildables[i]);

        updateProfile(buildables);
    }
};

static auto rCmdBuild = registerCommand<CmdBuild>("build");
