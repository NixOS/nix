#pragma once
///@file

#include "nix/store/store-api.hh"
#include "nix/store/gc-store.hh"
#include "nix/store/log-store.hh"

namespace nix {

template<template<typename> class F>
struct LocalFSStoreConfigT
{
    F<std::optional<Path>>::type rootDir;
    F<Path>::type stateDir;
    F<Path>::type logDir;
    F<Path>::type realStoreDir;
};

struct LocalFSStoreConfig : LocalFSStoreConfigT<config::PlainValue>
{
    const Store::Config & storeConfig;

    static config::SettingDescriptionMap descriptions();

    LocalFSStoreConfig(const Store::Config & storeConfig, const StoreConfig::Params &);

    /**
     * Used to override the `root` settings. Can't be done via modifying
     * `params` reliably because this parameter is unused except for
     * passing to base class constructors.
     *
     * @todo Make this less error-prone with new store settings system.
     */
    LocalFSStoreConfig(const Store::Config & storeConfig, PathView path, const StoreConfig::Params & params);
};

struct LocalFSStore : virtual Store, virtual GcStore, virtual LogStore
{
    using Config = LocalFSStoreConfig;

    const Config & config;

    inline static std::string operationName = "Local Filesystem Store";

    const static std::string drvsLogDir;

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
     * point to `toRealPath(store.printStorePath(storePath))`.
     *
     * How the permanent GC root corresponding to this symlink is
     * managed is implementation-specific.
     */
    virtual Path addPermRoot(const StorePath & storePath, const Path & gcRoot) = 0;

    virtual Path getRealStoreDir()
    {
        return config.realStoreDir;
    }

    Path toRealPath(const StorePath & storePath)
    {
        return toRealPath(printStorePath(storePath));
    }

    Path toRealPath(const Path & storePath)
    {
        assert(isInStore(storePath));
        return getRealStoreDir() + "/" + std::string(storePath, storeDir.size() + 1);
    }

    std::optional<std::string> getBuildLogExact(const StorePath & path) override;
};

} // namespace nix
