#pragma once

#include "store-api.hh"

namespace nix {

struct LocalFSStoreConfig : virtual StoreConfig
{
    using StoreConfig::StoreConfig;
    // FIXME: the (StoreConfig*) cast works around a bug in gcc that causes
    // it to omit the call to the Setting constructor. Clang works fine
    // either way.
    const PathSetting rootDir{(StoreConfig*) this, true, "",
        "root", "directory prefixed to all other paths"};
    const PathSetting stateDir{(StoreConfig*) this, false,
        rootDir != "" ? rootDir + "/nix/var/nix" : settings.nixStateDir,
        "state", "directory where Nix will store state"};
    const PathSetting logDir{(StoreConfig*) this, false,
        rootDir != "" ? rootDir + "/nix/var/log/nix" : settings.nixLogDir,
        "log", "directory where Nix will store state"};
};

class LocalFSStore : public virtual Store, public virtual LocalFSStoreConfig
{
public:

    const static string drvsLogDir;

    LocalFSStore(const Params & params);

    void narFromPath(StorePathOrDesc path, Sink & sink) override;
    ref<FSAccessor> getFSAccessor() override;

    /* Register a permanent GC root. */
    Path addPermRoot(const StorePath & storePath, const Path & gcRoot);

    virtual Path getRealStoreDir() { return storeDir; }

    Path toRealPath(const Path & storePath) override
    {
        assert(isInStore(storePath));
        return getRealStoreDir() + "/" + std::string(storePath, storeDir.size() + 1);
    }

    std::shared_ptr<std::string> getBuildLog(const StorePath & path) override;
};

}
