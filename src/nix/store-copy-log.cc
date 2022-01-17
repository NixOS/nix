#include "command.hh"
#include "shared.hh"
#include "store-api.hh"
#include "sync.hh"
#include "thread-pool.hh"

#include <atomic>

using namespace nix;

struct CmdCopyLog : virtual CopyCommand, virtual InstallablesCommand
{
    std::string description() override
    {
        return "copy build logs between Nix stores";
    }

    std::string doc() override
    {
        return
          #include "store-copy-log.md"
          ;
    }

    Category category() override { return catUtility; }

    void run(ref<Store> srcStore) override
    {
        auto dstStore = getDstStore();

        for (auto & path : toDerivations(srcStore, installables, true)) {
            if (auto log = srcStore->getBuildLog(path))
                dstStore->addBuildLog(path, *log);
            else
                throw Error("build log for '%s' is not available", srcStore->printStorePath(path));
        }
    }
};

static auto rCmdCopyLog = registerCommand2<CmdCopyLog>({"store", "copy-log"});
