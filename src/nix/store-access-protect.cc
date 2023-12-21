#include "command.hh"
#include "granular-access-store.hh"
#include "local-fs-store.hh"
#include "store-api.hh"
#include "store-cast.hh"

using namespace nix;

struct CmdStoreAccessProtect : StorePathsCommand
{
    std::string description() override
    {
        return "protect store paths";
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
                warn("There are some users or groups who have access to path %s; consider removing them with \n" ANSI_BOLD "nix store access revoke --all-entities %s" ANSI_NORMAL, localStore.printStorePath(path), localStore.printStorePath(path));
            if (!localStore.isValidPath(path)) warn("Path %s does not exist yet; permissions will be applied as soon as it is added to the store", localStore.printStorePath(path));
            status.isProtected = true;
            localStore.setAccessStatus(path, status, false);
        }
    }
};

static auto rStoreAccessProtect = registerCommand2<CmdStoreAccessProtect>({"store", "access", "protect"});
