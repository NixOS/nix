#include "command.hh"
#include "common-args.hh"
#include "store-api.hh"
#include "archive.hh"
#include "git.hh"

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
            .longName = "git",
            .shortName = 0,
            .description = "treat path as a git object",
            .handler = {&ingestionMethod, FileIngestionMethod::Git},
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

        Hash hash;
        switch (ingestionMethod) {
        case FileIngestionMethod::Recursive: {
            hash = narHash;
            break;
        }
        case FileIngestionMethod::Flat: {
            abort(); // not yet supported above
        }
        case FileIngestionMethod::Git: {
            hash = dumpGitHash(htSHA1, path);
            break;
        }
        }

        ValidPathInfo info(store->makeFixedOutputPath(ingestionMethod, hash, *namePart));
        info.narHash = narHash;
        info.narSize = sink.s->size();
        info.ca = std::optional { FixedOutputHash {
            .method = ingestionMethod,
            .hash = hash,
        } };

        if (!dryRun) {
            auto source = StringSource { *sink.s };
            store->addToStore(info, source);
        }

        logger->stdout("%s", store->printStorePath(info.path));
    }
};

static auto r1 = registerCommand<CmdAddToStore>("add-to-store");
