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

        auto narHash = hashString(htSHA256, *sink.s);

        ValidPathInfo info(store->makeFixedOutputPath(true, narHash, *namePart));
        info.narHash = narHash;
        info.narSize = sink.s->size();
        info.ca = makeFixedOutputCA(true, info.narHash);

        if (!dryRun)
            store->addToStore(info, sink.s);

        std::cout << fmt("%s\n", store->printStorePath(info.path));
    }
};

static auto r1 = registerCommand<CmdAddToStore>("add-to-store");
