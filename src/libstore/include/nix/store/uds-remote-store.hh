#pragma once
///@file

#include "nix/store/remote-store.hh"
#include "nix/store/remote-store-connection.hh"
#include "nix/store/indirect-root-store.hh"

namespace nix {

struct UDSRemoteStoreConfig : std::enable_shared_from_this<UDSRemoteStoreConfig>,
                              virtual LocalFSStoreConfig,
                              virtual RemoteStoreConfig
{
    // TODO(fzakaria): Delete this constructor once moved over to the factory pattern
    // outlined in https://github.com/NixOS/nix/issues/10766
    using LocalFSStoreConfig::LocalFSStoreConfig;
    using RemoteStoreConfig::RemoteStoreConfig;

    /**
     * @param authority is the socket path.
     */
    UDSRemoteStoreConfig(std::string_view scheme, std::string_view authority, const Params & params);

    UDSRemoteStoreConfig(const Params & params);

    static const std::string name()
    {
        return "Local Daemon Store";
    }

    static std::string doc();

    /**
     * The path to the unix domain socket.
     *
     * The default is `settings.nixDaemonSocketFile`, but we don't write
     * that below, instead putting in the constructor.
     */
    Path path;

    static StringSet uriSchemes()
    {
        return {"unix"};
    }

    ref<Store> openStore() const override;

    StoreReference getReference() const override;
};

struct UDSRemoteStore : virtual IndirectRootStore, virtual RemoteStore
{
    using Config = UDSRemoteStoreConfig;

    ref<const Config> config;

    UDSRemoteStore(ref<const Config>);

    ref<SourceAccessor> getFSAccessor(bool requireValidPath = true) override
    {
        return LocalFSStore::getFSAccessor(requireValidPath);
    }

    std::shared_ptr<SourceAccessor> getFSAccessor(const StorePath & path, bool requireValidPath = true) override
    {
        return LocalFSStore::getFSAccessor(path, requireValidPath);
    }

    void narFromPath(const StorePath & path, Sink & sink) override
    {
        Store::narFromPath(path, sink);
    }

    /**
     * Implementation of `IndirectRootStore::addIndirectRoot()` which
     * delegates to the remote store.
     *
     * The idea is that the client makes the direct symlink, so it is
     * owned managed by the client's user account, and the server makes
     * the indirect symlink.
     */
    void addIndirectRoot(const Path & path) override;

private:

    struct Connection : RemoteStore::Connection
    {
        AutoCloseFD fd;
        void closeWrite() override;
    };

    ref<RemoteStore::Connection> openConnection() override;
};

} // namespace nix
