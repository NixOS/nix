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

struct CmdDumpPath2 : Command
{
    Path path;

    CmdDumpPath2()
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
          #include "nar-dump-path.md"
          ;
    }

    void run() override
    {
        FdSink sink(STDOUT_FILENO);
        dumpPath(path, sink);
        sink.flush();
    }
};

static auto rDumpPath2 = registerCommand2<CmdDumpPath2>({"nar", "dump-path"});
