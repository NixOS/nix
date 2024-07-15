#pragma once
///@file

#include "remote-store.hh"
#include "remote-store-connection.hh"
#include "indirect-root-store.hh"

namespace nix {

const std::string UNIX_SCHEME = "unix";

struct UDSRemoteStoreConfig : virtual LocalFSStoreConfig, virtual RemoteStoreConfig
{

    // TODO(fzakaria): Delete this constructor once moved over to the factory pattern
    // outlined in https://github.com/NixOS/nix/issues/10766
    using LocalFSStoreConfig::LocalFSStoreConfig;
    using RemoteStoreConfig::RemoteStoreConfig;

    UDSRemoteStoreConfig(std::string_view scheme, std::string_view authority, const Params & params)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
        , RemoteStoreConfig(params)
    {
        assert(scheme == UNIX_SCHEME && "Scheme must be 'unix'");
        if (!authority.empty()) {
            path.emplace(authority);
        }
    }

    const std::string name() override { return "Local Daemon Store"; }

    std::string doc() override;

    // The path to the unix-domain sock
    // The default *could be* settings.nixDaemonSocketFile but that
    // won't pick up live changes unfortunately. This optional handling is instead
    // handled on opening of the connection
    std::optional<std::string> path;
};

class UDSRemoteStore : public virtual UDSRemoteStoreConfig
    , public virtual IndirectRootStore
    , public virtual RemoteStore
{
public:

    UDSRemoteStore(
        std::string_view scheme,
        // authority is the socket path
        std::string_view authority,
        const Params & params);

    std::string getUri() override;

    static std::set<std::string> uriSchemes()
    { return {UNIX_SCHEME}; }

    ref<SourceAccessor> getFSAccessor(bool requireValidPath = true) override
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

private:

    struct Connection : RemoteStore::Connection
    {
        AutoCloseFD fd;
        void closeWrite() override;
    };

    ref<RemoteStore::Connection> openConnection() override;

    std::string getPathOrDefault() const;
};

}
