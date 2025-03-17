#pragma once
///@file

#include "store-api.hh"
#include "gc-store.hh"
#include "log-store.hh"

namespace nix {

template<template<typename> class F>
struct LocalFSStoreConfigT
{
    F<std::optional<Path>> rootDir;
    F<Path> stateDir;
    F<Path> logDir;
    F<Path> realStoreDir;
};

struct LocalFSStoreConfig : LocalFSStoreConfigT<config::JustValue>
{
    const Store::Config & storeConfig;

    static config::SettingDescriptionMap descriptions();

    LocalFSStoreConfig(
        const Store::Config & storeConfig,
        const StoreReference::Params &);

    /**
     * Used to override the `root` settings. Can't be done via modifying
     * `params` reliably because this parameter is unused except for
     * passing to base class constructors.
     *
     * @todo Make this less error-prone with new store settings system.
     */
    LocalFSStoreConfig(
        const Store::Config & storeConfig,
        PathView path,
        const StoreReference::Params & params);
};

struct LocalFSStore :
    virtual Store,
    virtual GcStore,
    virtual LogStore
{
    using Config = LocalFSStoreConfig;

    const Config & config;

    inline static std::string operationName = "Local Filesystem Store";

    const static std::string drvsLogDir;

    LocalFSStore(const Config & params);

    void narFromPath(const StorePath & path, Sink & sink) override;
    ref<SourceAccessor> getFSAccessor(bool requireValidPath = true) override;

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

    virtual Path getRealStoreDir() { return config.realStoreDir; }

    Path toRealPath(const Path & storePath) override
    {
        assert(isInStore(storePath));
        return getRealStoreDir() + "/" + std::string(storePath, storeDir.size() + 1);
    }

    std::optional<std::string> getBuildLogExact(const StorePath & path) override;

};

}
