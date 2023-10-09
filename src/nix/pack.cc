#include "command.hh"
#include "store-api.hh"
#include "archive.hh"

using namespace nix;

struct CmdDumpPath : StorePathCommand
{
    std::string description() override
    {
        return "serialise a store path to stdout in NAR format";
    }

    std::string doc() override
    {
        return
          #include "store-dump-path.md"
          ;
    }

    void run(ref<Store> store, const StorePath & storePath) override
    {
        FdSink sink(STDOUT_FILENO);
        store->narFromPath(storePath, sink);
        sink.flush();
    }
};

static auto rDumpPath = registerCommand2<CmdDumpPath>({"store", "dump-path"});

struct CmdNarPack : Command
{
    Path path;

    CmdNarPack()
    {
        expectArgs({
            .label = "path",
            .handler = {&path},
            .completer = completePath
        });
    }

    std::string description() override
    {
        return "serialise a path to stdout in NAR format";
    }

    std::string doc() override
    {
        return
          #include "nar-pack.md"
          ;
    }

    void run() override
    {
        FdSink sink(STDOUT_FILENO);
        dumpPath(path, sink);
        sink.flush();
    }
};

static auto rNarPack = registerCommand2<CmdNarPack>({"nar", "pack"});
