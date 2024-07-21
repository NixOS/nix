#pragma once
///@file

#include "store-api.hh"
#include "gc-store.hh"
#include "log-store.hh"

namespace nix {

template<template<typename> class F>
struct LocalFSStoreConfigT
{
    const F<std::optional<Path>> rootDir;
    const F<Path> stateDir;
    const F<Path> logDir;
    const F<Path> realStoreDir;
};

struct LocalFSStoreConfig :
    virtual Store::Config,
    LocalFSStoreConfigT<config::JustValue>
{
    struct Descriptions :
        virtual Store::Config::Descriptions,
        LocalFSStoreConfigT<config::SettingInfo>
    {
        Descriptions();
    };

    static const Descriptions descriptions;

    /**
     * The other defaults depend on the choice of `storeDir` and `rootDir`
     */
    static LocalFSStoreConfigT<config::JustValue> defaults(
        const Store::Config &,
        const std::optional<Path> rootDir);

    LocalFSStoreConfig(const StoreReference::Params &);

    /**
     * Used to override the `root` settings. Can't be done via modifying
     * `params` reliably because this parameter is unused except for
     * passing to base class constructors.
     *
     * @todo Make this less error-prone with new store settings system.
     */
    LocalFSStoreConfig(PathView path, const StoreReference::Params & params);
};

struct LocalFSStore :
    virtual LocalFSStoreConfig,
    virtual Store,
    virtual GcStore,
    virtual LogStore
{
    using Config = LocalFSStoreConfig;

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

    virtual Path getRealStoreDir() { return realStoreDir; }

    Path toRealPath(const Path & storePath) override
    {
        assert(isInStore(storePath));
        return getRealStoreDir() + "/" + std::string(storePath, storeDir.size() + 1);
    }

    std::optional<std::string> getBuildLogExact(const StorePath & path) override;

};

}
