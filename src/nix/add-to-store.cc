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

        StringSink sink;
        dumpPath(path, sink);

        ValidPathInfo info;
        info.narHash = hashString(htSHA256, *sink.s);
        info.narSize = sink.s->size();
        info.path = store->makeFixedOutputPath(true, info.narHash, *namePart);
        info.ca = makeFixedOutputCA(true, info.narHash);

        if (!dryRun)
            store->addToStore(info, sink.s);

        std::cout << fmt("%s\n", info.path);
    }
};

static RegisterCommand r1(make_ref<CmdAddToStore>());
