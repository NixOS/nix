#include "command.hh"
#include "store-api.hh"
#include "local-fs-store.hh"
#include "store-cast.hh"

using namespace nix;

struct CmdStoreAccessUnprotect : StorePathsCommand
{
    std::string description() override
    {
        return "unprotect store paths";
    }

    std::string doc() override
    {
        return
          #include "store-repair.md"
          ;
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        auto & localStore = require<LocalGranularAccessStore>(*store);
        for (auto & path : storePaths) {
            auto status = localStore.getAccessStatus(path);
            if (!status.entities.empty())
                warn("There are still some users or groups who have access to path %s; consider removing them with \n" ANSI_BOLD "nix store access revoke --all-entities %s" ANSI_NORMAL, localStore.printStorePath(path), localStore.printStorePath(path));
            if (!localStore.isValidPath(path)) warn("Path %s does not exist yet; permissions will be applied as soon as it is added to the store", localStore.printStorePath(path));
            status.isProtected = false;
            localStore.setAccessStatus(path, status);
        }
    }
};

static auto rStoreAccessUnprotect = registerCommand2<CmdStoreAccessUnprotect>({"store", "access", "unprotect"});
