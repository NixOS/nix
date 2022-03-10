#include "command.hh"
#include "common-args.hh"
#include "store-api.hh"
#include "archive.hh"

using namespace nix;

struct CmdAddToStore : MixDryRun, StoreCommand
{
    Path path;
    std::optional<std::string> namePart;
    FileIngestionMethod ingestionMethod;

    CmdAddToStore()
    {
        // FIXME: completion
        expectArg("path", &path);

        addFlag({
            .longName = "name",
            .shortName = 'n',
            .description = "Override the name component of the store path. It defaults to the base name of *path*.",
            .labels = {"name"},
            .handler = {&namePart},
        });
    }

    void run(ref<Store> store) override
    {
        if (!namePart) namePart = baseNameOf(path);

        StringSink sink;
        dumpPath(path, sink);

        auto narHash = hashString(htSHA256, sink.s);

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
            narHash,
        };
        info.narSize = sink.s.size();

        if (!dryRun) {
            auto source = StringSource(sink.s);
            store->addToStore(info, source);
        }

        logger->cout("%s", store->printStorePath(info.path));
    }
};

struct CmdAddFile : CmdAddToStore
{
    CmdAddFile()
    {
        ingestionMethod = FileIngestionMethod::Flat;
    }

    std::string description() override
    {
        return "add a regular file to the Nix store";
    }

    std::string doc() override
    {
        return
          #include "add-file.md"
          ;
    }
};

struct CmdAddPath : CmdAddToStore
{
    CmdAddPath()
    {
        ingestionMethod = FileIngestionMethod::Recursive;
    }

    std::string description() override
    {
        return "add a path to the Nix store";
    }

    std::string doc() override
    {
        return
          #include "add-path.md"
          ;
    }
};

static auto rCmdAddFile = registerCommand2<CmdAddFile>({"store", "add-file"});
static auto rCmdAddPath = registerCommand2<CmdAddPath>({"store", "add-path"});
