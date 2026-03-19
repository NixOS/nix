#pragma once
///@file

#include "nix/store/remote-store.hh"
#include "nix/store/remote-store-connection.hh"
#include "nix/store/indirect-root-store.hh"

namespace nix {

/**
 * Get the daemon socket path for the given store configuration.
 *
 * Returns `NIX_DAEMON_SOCKET_PATH` if set, otherwise
 * `stateDir / "daemon-socket" / "socket"` where `stateDir` is from
 * the config (for LocalFSStore) or global settings.
 *
 * @note This function accepts any `Store::Config`, not just
 * `UDSRemoteStoreConfig`, because the daemon uses it to determine where
 * to listen for connections (configuring the daemon) and where to
 * connect to (configuring the client). The daemon may be serving any
 * type of store --- `UDSRemoteStoreConfig` is for *client* stores, not
 * for the server store.
 */
std::filesystem::path getDaemonSocketPath(const Store::Config & config);

struct UDSRemoteStoreConfig : std::enable_shared_from_this<UDSRemoteStoreConfig>,
                              virtual LocalFSStoreConfig,
                              virtual RemoteStoreConfig
{
    UDSRemoteStoreConfig(const std::filesystem::path & path, const Params & params);

    UDSRemoteStoreConfig(const Params & params);

    static const std::string name()
    {
        return "Local Daemon Store";
    }

    static std::string doc();

    /**
     * The path to the unix domain socket.
     *
     * The default is given by `getDaemonSocketPath`.
     */
    std::filesystem::path path;

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
    void addIndirectRoot(const std::filesystem::path & path) override;

private:

    struct Connection : RemoteStore::Connection
    {
        AutoCloseFD fd;
        void closeWrite() override;
    };

    ref<RemoteStore::Connection> openConnection() override;
};

} // namespace nix
