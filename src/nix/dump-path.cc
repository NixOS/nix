#include "command.hh"
#include "store-api.hh"

using namespace nix;

struct CmdDumpPath : StorePathCommand
{
    std::string name() override
    {
        return "dump-path";
    }

    std::string description() override
    {
        return "dump a store path to stdout (in NAR format)";
    }

    Examples examples() override
    {
        return {
            Example{
                "To get a NAR from the binary cache https://cache.nixos.org/:",
                "nix dump-path --store https://cache.nixos.org/ /nix/store/7crrmih8c52r8fbnqb933dxrsp44md93-glibc-2.25"
            },
        };
    }

    void run(ref<Store> store, const Path & storePath) override
    {
        FdSink sink(STDOUT_FILENO);
        store->narFromPath(storePath, sink);
        sink.flush();
    }
};

static RegisterCommand r1(make_ref<CmdDumpPath>());
