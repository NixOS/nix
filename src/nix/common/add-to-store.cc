#include "store-command.hh"
#include "common-args.hh"
#include "store-api.hh"
#include "archive.hh"

using namespace nix;

static FileIngestionMethod parseIngestionMethod(std::string_view input)
{
    if (input == "flat") {
        return FileIngestionMethod::Flat;
    } else if (input == "nar") {
        return FileIngestionMethod::Recursive;
    } else {
        throw UsageError("Unknown hash mode '%s', expect `flat` or `nar`");
    }
}

struct CmdAddToStore : MixDryRun, StoreCommand
{
    Path path;
    std::optional<std::string> namePart;
    FileIngestionMethod ingestionMethod = FileIngestionMethod::Recursive;

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

        addFlag({
            .longName  = "mode",
            .shortName = 'n',
            .description = R"(
    How to compute the hash of the input.
    One of:

    - `nar` (the default): Serialises the input as an archive (following the [_Nix Archive Format_](https://edolstra.github.io/pubs/phd-thesis.pdf#page=101)) and passes that to the hash function.

    - `flat`: Assumes that the input is a single file and directly passes it to the hash function;
            )",
            .labels = {"hash-mode"},
            .handler = {[this](std::string s) {
                this->ingestionMethod = parseIngestionMethod(s);
            }},
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
            std::move(*namePart),
            FixedOutputInfo {
                .method = std::move(ingestionMethod),
                .hash = std::move(hash),
                .references = {},
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

struct CmdAdd : CmdAddToStore
{

    std::string description() override
    {
        return "Add a file or directory to the Nix store";
    }

    std::string doc() override
    {
        return
          #include "add.md"
          ;
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
        return "Deprecated. Use [`nix store add --mode flat`](@docroot@/command-ref/new-cli/nix3-store-add.md) instead.";
    }
};

struct CmdAddPath : CmdAddToStore
{
    std::string description() override
    {
        return "Deprecated alias to [`nix store add`](@docroot@/command-ref/new-cli/nix3-store-add.md).";
    }
};

static auto rCmdAddFile = registerCommand2<CmdAddFile>({"store", "add-file"});
static auto rCmdAddPath = registerCommand2<CmdAddPath>({"store", "add-path"});
static auto rCmdAdd = registerCommand2<CmdAdd>({"store", "add"});
