#pragma once
///@file

#include "store-api.hh"
#include "gc-store.hh"
#include "log-store.hh"

namespace nix {

struct LocalFSStoreConfig : virtual StoreConfig
{
    using StoreConfig::StoreConfig;

    // FIXME: the (StoreConfig*) cast works around a bug in gcc that causes
    // it to omit the call to the Setting constructor. Clang works fine
    // either way.

    const OptionalPathSetting rootDir{(StoreConfig*) this, std::nullopt,
        "root",
        "Directory prefixed to all other paths."};

    const PathSetting stateDir{(StoreConfig*) this,
        rootDir.get() ? *rootDir.get() + "/nix/var/nix" : settings.nixStateDir,
        "state",
        "Directory where Nix will store state."};

    const PathSetting logDir{(StoreConfig*) this,
        rootDir.get() ? *rootDir.get() + "/nix/var/log/nix" : settings.nixLogDir,
        "log",
        "directory where Nix will store log files."};

    const PathSetting realStoreDir{(StoreConfig*) this,
        rootDir.get() ? *rootDir.get() + "/nix/store" : storeDir, "real",
        "Physical path of the Nix store."};
};

class LocalFSStore : public virtual LocalFSStoreConfig,
    public virtual Store,
    public virtual GcStore,
    public virtual LogStore
{
public:

    const static std::string drvsLogDir;

    LocalFSStore(const Params & params);

    void narFromPath(const StorePath & path, Sink & sink) override;
    ref<FSAccessor> getFSAccessor() override;

    /**
     * Register a permanent GC root.
     */
    Path addPermRoot(const StorePath & storePath, const Path & gcRoot);

    virtual Path getRealStoreDir() { return realStoreDir; }

    Path toRealPath(const Path & storePath) override
    {
        assert(isInStore(storePath));
        return getRealStoreDir() + "/" + std::string(storePath, storeDir.size() + 1);
    }

    std::optional<std::string> getBuildLogExact(const StorePath & path) override;

};

}
