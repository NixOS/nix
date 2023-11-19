#pragma once
///@file

#include "store-api.hh"
#include "gc-store.hh"
#include "log-store.hh"

namespace nix {

struct LocalFSStoreConfig : virtual StoreConfig
{
    using StoreConfig::StoreConfig;

    const OptionalPathSetting rootDir{this, std::nullopt,
        "root",
        "Directory prefixed to all other paths."};

    const PathSetting stateDir{this,
        rootDir.get() ? *rootDir.get() + "/nix/var/nix" : settings.nixStateDir,
        "state",
        "Directory where Nix will store state."};

    const PathSetting logDir{this,
        rootDir.get() ? *rootDir.get() + "/nix/var/log/nix" : settings.nixLogDir,
        "log",
        "directory where Nix will store log files."};

    const PathSetting realStoreDir{this,
        rootDir.get() ? *rootDir.get() + "/nix/store" : storeDir, "real",
        "Physical path of the Nix store."};
};

class LocalFSStore : public virtual LocalFSStoreConfig,
    public virtual Store,
    public virtual GcStore,
    public virtual LogStore
{
public:
    inline static std::string operationName = "Local Filesystem Store";

    const static std::string drvsLogDir;

    LocalFSStore(const Params & params);

    void narFromPath(const StorePath & path, Sink & sink) override;
    ref<SourceAccessor> getFSAccessor(bool requireValidPath) override;

    /**
     * Creates symlink from the `gcRoot` to the `storePath` and
     * registers the `gcRoot` as a permanent GC root. The `gcRoot`
     * symlink lives outside the store and is created and owned by the
     * user.
     *
     * @param gcRoot The location of the symlink.
     *
     * @param storePath The store object being rooted. The symlink will
     * point to `toRealPath(store.printStorePath(storePath))`.
     *
     * How the permanent GC root corresponding to this symlink is
     * managed is implementation-specific.
     */
    virtual Path addPermRoot(const StorePath & storePath, const Path & gcRoot) = 0;

    virtual Path getRealStoreDir() { return realStoreDir; }

    Path toRealPath(const Path & storePath) override
    {
        assert(isInStore(storePath));
        return getRealStoreDir() + "/" + std::string(storePath, storeDir.size() + 1);
    }

    std::optional<std::string> getBuildLogExact(const StorePath & path) override;

    /**
     * @todo this was moved to `LocalFSStore` from `LocalStore` because
     * building needed it. Instead of just blindly moving it, we should
     * should consider the division of labor and trust between the
     * builder and the store.
     */
    virtual void registerValidPaths(const ValidPathInfos & infos)
    { unsupported("registerValidPaths"); }

    /**
     * Optimise a single store path. Optionally, test the encountered
     * symlinks for corruption.
     *
     * @todo this was moved to `LocalFSStore` from `LocalStore` because
     * building needed it. Instead of just blindly moving it, we should
     * should consider the division of labor and trust between the
     * builder and the store.
     */
    virtual void optimisePath(const Path & path, RepairFlag repair)
    { unsupported("optimisePath"); }

    /**
     * Add signatures to a ValidPathInfo or Realisation using the secret keys
     * specified by the ‘secret-key-files’ option.
     *
     * @todo this was moved to `LocalFSStore` from `LocalStore` because
     * building needed it. Instead of just blindly moving it, we should
     * should consider the division of labor and trust between the
     * builder and the store.
     */
    virtual void signPathInfo(ValidPathInfo & info)
    { unsupported("signPathInfo"); }
    virtual void signRealisation(Realisation &)
    { unsupported("signRealisation"); }
};

}
