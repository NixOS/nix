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
    bool git = false;

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
            .description = "treat path as a git object",
            .handler = {&this->git, true},
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

        auto ingestionMethod = git ? FileIngestionMethod::Git : FileIngestionMethod::Recursive;

        StringSink sink;
        if (git)
            dumpGit(path, sink);
        else
            dumpPath(path, sink);

        auto narHash = hashString(git ? htSHA1 : htSHA256, *sink.s);

        ValidPathInfo info(store->makeFixedOutputPath(ingestionMethod, narHash, *namePart));
        info.narHash = narHash;
        info.narSize = sink.s->size();
        info.ca = makeFixedOutputCA(ingestionMethod, info.narHash);

        if (!dryRun)
            store->addToStore(*namePart, path, ingestionMethod, git ? htSHA1 : htSHA256);

        logger->stdout("%s", store->printStorePath(info.path));
    }
};

static auto r1 = registerCommand<CmdAddToStore>("add-to-store");
