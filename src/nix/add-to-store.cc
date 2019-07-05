#include "command.hh"
#include "common-args.hh"
#include "store-api.hh"
#include "archive.hh"

using namespace nix;

struct CmdAddToStore : MixDryRun, StoreCommand
{
    Path path;
    std::optional<std::string> namePart;

    CmdAddToStore()
    {
        expectArg("path", &path);

        mkFlag()
            .longName("name")
            .shortName('n')
            .description("name component of the store path")
            .labels({"name"})
            .dest(&namePart);
    }

    std::string name() override
    {
        return "add-to-store";
    }

    std::string description() override
    {
        return "add a path to the Nix store";
    }

    Examples examples() override
    {
        return {
        };
    }

    void run(ref<Store> store) override
    {
        if (!namePart) namePart = baseNameOf(path);

        if (!dryRun) {
            const string result = store->addToStore(*namePart, path);
            std::cout << fmt("%s\n", result);
        }
        else {
            const Hash narHash = hashPath(htSHA256, path).first;
            const string result = store->makeFixedOutputPath(true, narHash, *namePart);

            std::cout << fmt("%s\n", result);
        }
    }
};

static RegisterCommand r1(make_ref<CmdAddToStore>());
