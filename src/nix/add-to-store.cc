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

    std::string doc() override
    {
        return R"(
          # Description

          Copy the file or directory *path* to the Nix store, and
          print the resulting store path on standard output.

          > **Warning**
          >
          > The resulting store path is not registered as a garbage
          > collector root, so it could be deleted before you have a
          > chance to register it.

          # Examples

          Add a regular file to the store:

          ```console
          # echo foo > bar
          # nix add-to-store --flat ./bar
          /nix/store/cbv2s4bsvzjri77s2gb8g8bpcb6dpa8w-bar
          # cat /nix/store/cbv2s4bsvzjri77s2gb8g8bpcb6dpa8w-bar
          foo
          ```
        )";
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
            store->makeFixedOutputPath(ingestionMethod, hash, *namePart),
            narHash,
        };
        info.narSize = sink.s->size();
        info.ca = std::optional { FixedOutputHash {
            .method = ingestionMethod,
            .hash = hash,
        } };

        if (!dryRun) {
            auto source = StringSource { *sink.s };
            store->addToStore(info, source);
        }

        logger->cout("%s", store->printStorePath(info.path));
    }
};

static auto rCmdAddToStore = registerCommand<CmdAddToStore>("add-to-store");
