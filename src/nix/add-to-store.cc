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

        Hash hash { htSHA256 }; // throwaway def to appease C++
        switch (ingestionMethod) {
        case FileIngestionMethod::Recursive: {
            hash = narHash;
            break;
        }
        case FileIngestionMethod::Flat: {
            HashSink hsink(htSHA256);
            readFile(path, hsink);
            hash = hsink.finish().first;
            break;
        }
        case FileIngestionMethod::Git: {
            hash = dumpGitHash(htSHA1, path);
            break;
        }
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
                    .references = {},
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

struct CmdAddGit : CmdAddToStore
{
    CmdAddGit()
    {
        ingestionMethod = FileIngestionMethod::Git;
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
static auto rCmdAddGit = registerCommand2<CmdAddGit>({"store", "add-git"});
