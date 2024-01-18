#pragma once
///@file

#include "remote-store.hh"
#include "remote-store-connection.hh"
#include "indirect-root-store.hh"

namespace nix {

struct UnmountedUDSRemoteStoreConfig : virtual RemoteStoreConfig
{
    UnmountedUDSRemoteStoreConfig(const Params & params)
        : StoreConfig(params)
        , RemoteStoreConfig(params)
    {
    }

    const std::string name() override { return "Local Daemon Store without filesystem mounted"; }

    std::string doc() override;
};

class UnmountedUDSRemoteStore : public virtual UnmountedUDSRemoteStoreConfig
    , public virtual RemoteStore
{
public:

    UnmountedUDSRemoteStore(const Params & params);
    UnmountedUDSRemoteStore(const std::string scheme, std::string path, const Params & params);

    std::string getUri() override;

    static std::set<std::string> uriSchemes()
    { return {"unmounted-unix"}; }

    // FIXME extend daemon protocol, move implementation to RemoteStore
    std::optional<std::string> getBuildLogExact(const StorePath & path) override
    { unsupported("getBuildLogExact"); }

protected:

    struct Connection : RemoteStore::Connection
    {
        AutoCloseFD fd;
        void closeWrite() override;
    };

    ref<RemoteStore::Connection> openConnection() override;
    std::optional<std::string> path;
};


struct UDSRemoteStoreConfig : virtual LocalFSStoreConfig, virtual UnmountedUDSRemoteStoreConfig
{
    UDSRemoteStoreConfig(const Params & params)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
        , RemoteStoreConfig(params)
        , UnmountedUDSRemoteStoreConfig(params)
    {
    }

    const std::string name() override { return "Local Daemon Store"; }

    std::string doc() override;
};

class UDSRemoteStore : public virtual UDSRemoteStoreConfig
    , public virtual IndirectRootStore
    , public virtual RemoteStore
    , public virtual UnmountedUDSRemoteStore
{
public:

    UDSRemoteStore(const Params & params);
    UDSRemoteStore(const std::string scheme, std::string path, const Params & params);

    static std::set<std::string> uriSchemes()
    { return {"unix"}; }

    std::optional<std::string> getBuildLogExact(const StorePath & path) override
    { return LocalFSStore::getBuildLogExact(path); }

    ref<SourceAccessor> getFSAccessor(bool requireValidPath) override
    { return LocalFSStore::getFSAccessor(requireValidPath); }

    void narFromPath(const StorePath & path, Sink & sink) override
    { LocalFSStore::narFromPath(path, sink); }

    /**
     * Implementation of `IndirectRootStore::addIndirectRoot()` which
     * delegates to the remote store.
     *
     * The idea is that the client makes the direct symlink, so it is
     * owned managed by the client's user account, and the server makes
     * the indirect symlink.
     */
    void addIndirectRoot(const Path & path) override;
};

}
