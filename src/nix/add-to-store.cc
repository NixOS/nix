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

        addFlag({
            .longName = "name",
            .shortName = 'n',
            .description = "name component of the store path",
            .labels = {"name"},
            .handler = {&namePart},
        });
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

    Category category() override { return catUtility; }

    void run(ref<Store> store) override
    {
        if (!namePart) namePart = baseNameOf(path);

        StringSink sink;
        dumpPath(path, sink);

        auto narHash = hashString(htSHA256, *sink.s);

        ValidPathInfo info(store->makeFixedOutputPath(FileIngestionMethod::Recursive, narHash, *namePart));
        info.narHash = narHash;
        info.narSize = sink.s->size();
        info.ca = makeFixedOutputCA(FileIngestionMethod::Recursive, info.narHash);

        if (!dryRun) {
            auto source = StringSource { *sink.s };
            store->addToStore(info, source);
        }

        logger->stdout("%s", store->printStorePath(info.path));
    }
};

static auto r1 = registerCommand<CmdAddToStore>("add-to-store");
