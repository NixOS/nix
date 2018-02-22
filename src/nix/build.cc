#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"

using namespace nix;

struct CmdBuild : MixDryRun, InstallablesCommand
{
    Path outLink = "result";

    CmdBuild()
    {
        mkFlag()
            .longName("out-link")
            .shortName('o')
            .description("path of the symlink to the build result")
            .labels({"path"})
            .dest(&outLink);

        mkFlag()
            .longName("no-link")
            .description("do not create a symlink to the build result")
            .set(&outLink, Path(""));
    }

    std::string name() override
    {
        return "build";
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
        };
    }

    void run(ref<Store> store) override
    {
        auto buildables = build(store, dryRun ? DryRun : Build, installables);

        if (dryRun) return;

        for (size_t i = 0; i < buildables.size(); ++i) {
            auto & b(buildables[i]);

            if (outLink != "")
                for (auto & output : b.outputs)
                    if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>()) {
                        std::string symlink = outLink;
                        if (i) symlink += fmt("-%d", i);
                        if (output.first != "out") symlink += fmt("-%s", output.first);
                        store2->addPermRoot(output.second, absPath(symlink), true);
                    }
        }
    }
};

static RegisterCommand r1(make_ref<CmdBuild>());
