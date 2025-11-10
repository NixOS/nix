#include "nix/store/ssh-store.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/store/remote-store-connection.hh"
#include "nix/util/source-accessor.hh"
#include "nix/util/archive.hh"
#include "nix/store/worker-protocol.hh"
#include "nix/store/worker-protocol-impl.hh"
#include "nix/util/pool.hh"
#include "nix/store/ssh.hh"
#include "nix/store/store-registration.hh"

namespace nix {

SSHStoreConfig::SSHStoreConfig(std::string_view scheme, std::string_view authority, const Params & params)
    : Store::Config{params}
    , RemoteStore::Config{params}
    , CommonSSHStoreConfig{scheme, authority, params}
{
}

std::string SSHStoreConfig::doc()
{
    return
#include "ssh-store.md"
        ;
}

StoreReference SSHStoreConfig::getReference() const
{
    return {
        .variant =
            StoreReference::Specified{
                .scheme = *uriSchemes().begin(),
                .authority = authority.to_string(),
            },
        .params = getQueryParams(),
    };
}

struct SSHStore : virtual RemoteStore
{
    using Config = SSHStoreConfig;

    ref<const Config> config;

    SSHStore(ref<const Config> config)
        : Store{*config}
        , RemoteStore{*config}
        , config{config}
        , master(config->createSSHMaster(
              // Use SSH master only if using more than 1 connection.
              connections->capacity() > 1))
    {
    }

    // FIXME extend daemon protocol, move implementation to RemoteStore
    std::optional<std::string> getBuildLogExact(const StorePath & path) override
    {
        unsupported("getBuildLogExact");
    }

protected:

    struct Connection : RemoteStore::Connection
    {
        std::unique_ptr<SSHMaster::Connection> sshConn;

        void closeWrite() override
        {
            sshConn->in.close();
        }
    };

    ref<RemoteStore::Connection> openConnection() override;

    std::vector<std::string> extraRemoteProgramArgs;

    SSHMaster master;

    void setOptions(RemoteStore::Connection & conn) override {
        /* TODO Add a way to explicitly ask for some options to be
           forwarded. One option: A way to query the daemon for its
           settings, and then a series of params to SSHStore like
           forward-cores or forward-overridden-cores that only
           override the requested settings.
        */
    };
};

MountedSSHStoreConfig::MountedSSHStoreConfig(StringMap params)
    : StoreConfig(params)
    , RemoteStoreConfig(params)
    , CommonSSHStoreConfig(params)
    , SSHStoreConfig(params)
    , LocalFSStoreConfig(params)
{
}

MountedSSHStoreConfig::MountedSSHStoreConfig(std::string_view scheme, std::string_view host, StringMap params)
    : StoreConfig(params)
    , RemoteStoreConfig(params)
    , CommonSSHStoreConfig(scheme, host, params)
    , SSHStoreConfig(scheme, host, params)
    , LocalFSStoreConfig(params)
{
}

std::string MountedSSHStoreConfig::doc()
{
    return
#include "mounted-ssh-store.md"
        ;
}

/**
 * The mounted ssh store assumes that filesystems on the remote host are
 * shared with the local host. This means that the remote nix store is
 * available locally and is therefore treated as a local filesystem
 * store.
 *
 * MountedSSHStore is very similar to UDSRemoteStore --- ignoring the
 * superficial difference of SSH vs Unix domain sockets, they both are
 * accessing remote stores, and they both assume the store will be
 * mounted in the local filesystem.
 *
 * The difference lies in how they manage GC roots. See addPermRoot
 * below for details.
 */
struct MountedSSHStore : virtual SSHStore, virtual LocalFSStore
{
    using Config = MountedSSHStoreConfig;

    MountedSSHStore(ref<const Config> config)
        : Store{*config}
        , RemoteStore{*config}
        , SSHStore{config}
        , LocalFSStore{*config}
    {
        extraRemoteProgramArgs = {
            "--process-ops",
        };
    }

    void narFromPath(const StorePath & path, Sink & sink) override
    {
        return Store::narFromPath(path, sink);
    }

    ref<SourceAccessor> getFSAccessor(bool requireValidPath) override
    {
        return LocalFSStore::getFSAccessor(requireValidPath);
    }

    std::shared_ptr<SourceAccessor> getFSAccessor(const StorePath & path, bool requireValidPath) override
    {
        return LocalFSStore::getFSAccessor(path, requireValidPath);
    }

    std::optional<std::string> getBuildLogExact(const StorePath & path) override
    {
        return LocalFSStore::getBuildLogExact(path);
    }

    /**
     * This is the key difference from UDSRemoteStore: UDSRemote store
     * has the client create the direct root, and the remote side create
     * the indirect root.
     *
     * We could also do that, but the race conditions (will the remote
     * side see the direct root the client made?) seems bigger.
     *
     * In addition, the remote-side will have a process associated with
     * the authenticating user handling the connection (even if there
     * is a system-wide daemon or similar). This process can safely make
     * the direct and indirect roots without there being such a risk of
     * privilege escalation / symlinks in directories owned by the
     * originating requester that they cannot delete.
     */
    Path addPermRoot(const StorePath & path, const Path & gcRoot) override
    {
        auto conn(getConnection());
        conn->to << WorkerProto::Op::AddPermRoot;
        WorkerProto::write(*this, *conn, path);
        WorkerProto::write(*this, *conn, gcRoot);
        conn.processStderr();
        return readString(conn->from);
    }
};

ref<Store> SSHStore::Config::openStore() const
{
    return make_ref<SSHStore>(ref{shared_from_this()});
}

ref<Store> MountedSSHStore::Config::openStore() const
{
    return make_ref<MountedSSHStore>(ref{std::dynamic_pointer_cast<const MountedSSHStore::Config>(shared_from_this())});
}

ref<RemoteStore::Connection> SSHStore::openConnection()
{
    auto conn = make_ref<Connection>();
    Strings command = config->remoteProgram.get();
    command.push_back("--stdio");
    if (config->remoteStore.get() != "") {
        command.push_back("--store");
        command.push_back(config->remoteStore.get());
    }
    command.insert(command.end(), extraRemoteProgramArgs.begin(), extraRemoteProgramArgs.end());
    conn->sshConn = master.startCommand(std::move(command));
    conn->to = FdSink(conn->sshConn->in.get());
    conn->from = FdSource(conn->sshConn->out.get());
    return conn;
}

static RegisterStoreImplementation<SSHStore::Config> regSSHStore;
static RegisterStoreImplementation<MountedSSHStore::Config> regMountedSSHStore;

} // namespace nix
