#pragma once
///@file

#include "nix/store/store-api.hh"
#include "nix/store/gc-store.hh"
#include "nix/store/log-store.hh"

namespace nix {

struct LocalFSStoreConfig : virtual StoreConfig
{
private:
    static Setting<std::optional<std::filesystem::path>>
    makeRootDirSetting(LocalFSStoreConfig & self, std::optional<std::filesystem::path> defaultValue)
    {
        return {
            &self,
            std::move(defaultValue),
            "root",
            "Directory prefixed to all other paths.",
        };
    }

public:
    LocalFSStoreConfig(const Params & params)
        : StoreConfig(params, FilePathType::Native)
    {
    }

    /**
     * Used to override the `root` settings. Can't be done via modifying
     * `params` reliably because this parameter is unused except for
     * passing to base class constructors.
     *
     * @todo Make this less error-prone with new store settings system.
     */
    LocalFSStoreConfig(const std::filesystem::path & path, const Params & params);

    Setting<std::optional<std::filesystem::path>> rootDir = makeRootDirSetting(*this, std::nullopt);

private:

    /**
     * An indirection so that we don't need to refer to global settings
     * in headers.
     */
    static std::filesystem::path getDefaultStateDir();

    /**
     * An indirection so that we don't need to refer to global settings
     * in headers.
     */
    static std::filesystem::path getDefaultLogDir();

public:

    Setting<std::filesystem::path> stateDir{
        this,
        rootDir.get() ? *rootDir.get() / "nix" / "var" / "nix" : getDefaultStateDir(),
        "state",
        "Directory where Nix stores state.",
    };

    Setting<std::filesystem::path> logDir{
        this,
        rootDir.get() ? *rootDir.get() / "nix" / "var" / "log" / "nix" : getDefaultLogDir(),
        "log",
        "directory where Nix stores log files.",
    };

    Setting<std::filesystem::path> realStoreDir{
        this,
        rootDir.get() ? *rootDir.get() / "nix" / "store" : std::filesystem::path{storeDir},
        "real",
        "Physical path of the Nix store.",
    };
};

struct alignas(8) /* Work around ASAN failures on i686-linux. */
    LocalFSStore : virtual Store,
                   virtual GcStore,
                   virtual LogStore
{
    using Config = LocalFSStoreConfig;

    const Config & config;

    inline static std::string operationName = "Local Filesystem Store";

    const static std::filesystem::path drvsLogDir;

    LocalFSStore(const Config & params);

    ref<SourceAccessor> getFSAccessor(bool requireValidPath = true) override;
    std::shared_ptr<SourceAccessor> getFSAccessor(const StorePath & path, bool requireValidPath = true) override;

    /**
     * Creates symlink from the `gcRoot` to the `storePath` and
     * registers the `gcRoot` as a permanent GC root. The `gcRoot`
     * symlink lives outside the store and is created and owned by the
     * user.
     *
     * @param gcRoot The location of the symlink.
     *
     * @param storePath The store object being rooted. The symlink will
     * point to `toRealPath(storePath)`.
     *
     * How the permanent GC root corresponding to this symlink is
     * managed is implementation-specific.
     */
    virtual std::filesystem::path addPermRoot(const StorePath & storePath, const std::filesystem::path & gcRoot) = 0;

    virtual std::filesystem::path getRealStoreDir()
    {
        return config.realStoreDir;
    }

    std::filesystem::path toRealPath(const StorePath & storePath)
    {
        return getRealStoreDir() / storePath.to_string();
    }

    std::optional<std::string> getBuildLogExact(const StorePath & path) override;
};

} // namespace nix
