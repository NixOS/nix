#include "nix/cmd/command.hh"
#include "nix/main/shared.hh"
#include "nix/store/build-control-store.hh"
#include "nix/store/store-cast.hh"

namespace nix {

struct CmdStoreKillBuild : StoreCommand
{
    std::string path;

    CmdStoreKillBuild()
    {
        expectArg("path", &path);
    }

    std::string description() override
    {
        return "terminate the active build of an output or registered derivation";
    }

    std::string doc() override
    {
        return
#include "store-kill-build.md"
            ;
    }

    void run(ref<Store> store) override
    {
        auto storePath = store->parseStorePath(path);
        auto & buildControlStore = require<BuildControlStore>(*store);
        auto pid = buildControlStore.killBuild(storePath);
        if (!pid) {
            warn("no process holds an output lock for '%s'", store->printStorePath(storePath));
            return;
        }

        notice(
            "process %d released the output locks for '%s' after being signalled",
            *pid,
            store->printStorePath(storePath));
    }
};

static auto rCmdStoreKillBuild = registerCommand2<CmdStoreKillBuild>({"store", "kill-build"});

} // namespace nix
