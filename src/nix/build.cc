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
            .arity(1)
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

    void run(ref<Store> store) override
    {
        auto buildables = toBuildables(store, dryRun ? DryRun : Build);

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
