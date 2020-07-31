#include "command.hh"
#include "common-args.hh"
#include "store-api.hh"
#include "archive.hh"

using namespace nix;

struct CmdAddToStore : MixDryRun, StoreCommand
{
    Path path;
    std::optional<std::string> namePart;
    FileIngestionMethod ingestionMethod = FileIngestionMethod::Recursive;

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

        addFlag({
            .longName = "flat",
            .shortName = 0,
            .description = "add flat file to the Nix store",
            .handler = {&ingestionMethod, FileIngestionMethod::Flat},
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

        Hash hash = narHash;
        if (ingestionMethod == FileIngestionMethod::Flat) {
            HashSink hsink(htSHA256);
            readFile(path, hsink);
            hash = hsink.finish().first;
        }

        ValidPathInfo info {
            *store,
            StorePathDescriptor {
                .name = *namePart,
                .info = FixedOutputInfo {
                    {
                        .method = std::move(ingestionMethod),
                        .hash = std::move(hash),
                    },
                    {},
                },
            },
        };
        info.narHash = narHash;
        info.narSize = sink.s->size();

        if (!dryRun) {
            auto source = StringSource { *sink.s };
            store->addToStore(info, source);
        }

        logger->stdout("%s", store->printStorePath(info.path));
    }
};

static auto r1 = registerCommand<CmdAddToStore>("add-to-store");
