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

        StorePathSet drvPaths;

        for (auto & i : installables)
            for (auto & drvPath : i->toDrvPaths(getEvalStore()))
                drvPaths.insert(drvPath);

        for (auto & drvPath : drvPaths) {
            if (auto log = srcStore->getBuildLog(drvPath))
                dstStore->addBuildLog(drvPath, *log);
            else
                throw Error("build log for '%s' is not available", srcStore->printStorePath(drvPath));
        }
    }
};

static auto rCmdCopyLog = registerCommand2<CmdCopyLog>({"store", "copy-log"});
