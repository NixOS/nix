#include "ssh-store.hh"
#include "local-fs-store.hh"
#include "remote-store-connection.hh"
#include "source-accessor.hh"
#include "archive.hh"
#include "worker-protocol.hh"
#include "worker-protocol-impl.hh"
#include "pool.hh"
#include "ssh.hh"
#include "config-parse-impl.hh"
#include "store-registration.hh"

namespace nix {

static const SSHStoreConfigT<config::SettingInfo> sshStoreConfigDescriptions = {
    .remoteProgram{
        .name = "remote-program",
        .description = "Path to the `nix-daemon` executable on the remote machine.",
    },
};


#define SSH_STORE_CONFIG_FIELDS(X) \
    X(remoteProgram)


MAKE_PARSE(SSHStoreConfig, sshStoreConfig, SSH_STORE_CONFIG_FIELDS)


static SSHStoreConfigT<config::JustValue> sshStoreConfigDefaults()
{
    return {
        .remoteProgram = {{"nix-daemon"}},
    };
}


MAKE_APPLY_PARSE(SSHStoreConfig, sshStoreConfig, SSH_STORE_CONFIG_FIELDS)


config::SettingDescriptionMap SSHStoreConfig::descriptions()
{
    config::SettingDescriptionMap ret;
    ret.merge(StoreConfig::descriptions());
    ret.merge(CommonSSHStoreConfig::descriptions());
    ret.merge(RemoteStoreConfig::descriptions());
    {
        constexpr auto & descriptions = sshStoreConfigDescriptions;
        auto defaults = sshStoreConfigDefaults();
        ret.merge(decltype(ret){
            SSH_STORE_CONFIG_FIELDS(DESC_ROW)
        });
    }
    //ret.merge(LocalFSStoreConfig::descriptions());
    return ret;
}


static std::optional<LocalFSStore::Config> getMounted(
    const Store::Config & storeConfig,
    const StoreReference::Params & params)
{
    auto mountedParamsOpt = optionalValueAt(params, "mounted");
    if (!mountedParamsOpt) return {};
    auto * mountedParamsP = getNullable(*mountedParamsOpt);
    if (!mountedParamsP) return {};
    auto & mountedParams = getObject(*mountedParamsP);
    return {{storeConfig, mountedParams}};
}


SSHStoreConfig::SSHStoreConfig(
    std::string_view scheme,
    std::string_view authority,
    const StoreReference::Params & params)
    : Store::Config{params}
    , RemoteStore::Config{*this, params}
    , CommonSSHStoreConfig{scheme, authority, params}
    , SSHStoreConfigT<config::JustValue>{sshStoreConfigApplyParse(params)}
    , mounted{getMounted(*this, params)}
{
}


std::string SSHStoreConfig::doc()
{
      #include "ssh-store.md"
      ;
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

    std::string getUri() override
    {
        return *Config::uriSchemes().begin() + "://" + host;
    }

    // FIXME extend daemon protocol, move implementation to RemoteStore
    std::optional<std::string> getBuildLogExact(const StorePath & path) override
    { unsupported("getBuildLogExact"); }

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

    std::string host;

    std::vector<std::string> extraRemoteProgramArgs;

    SSHMaster master;

    void setOptions(RemoteStore::Connection & conn) override
    {
        /* TODO Add a way to explicitly ask for some options to be
           forwarded. One option: A way to query the daemon for its
           settings, and then a series of params to SSHStore like
           forward-cores or forward-overridden-cores that only
           override the requested settings.
        */
    };
};


/**
 * The mounted ssh store assumes that filesystems on the remote host are
 * shared with the local host. This means that the remote nix store is
 * available locally and is therefore treated as a local filesystem
 * store.
 *
 * MountedSSHStore is very similar to UDSRemoteStore --- ignoring the
 * superficial differnce of SSH vs Unix domain sockets, they both are
 * accessing remote stores, and they both assume the store will be
 * mounted in the local filesystem.
 *
 * The difference lies in how they manage GC roots. See addPermRoot
 * below for details.
 */
struct MountedSSHStore : virtual SSHStore, virtual LocalFSStore
{
    using Config = SSHStore::Config;

    const LocalFSStore::Config & mountedConfig;

    MountedSSHStore(ref<const Config> config, const LocalFSStore::Config & mountedConfig)
        : Store{*config}
        , RemoteStore{*config}
        , SSHStore{config}
        , LocalFSStore{mountedConfig}
        , mountedConfig{mountedConfig}
    {
        extraRemoteProgramArgs = {
            "--process-ops",
        };
    }

    void narFromPath(const StorePath & path, Sink & sink) override
    {
        return LocalFSStore::narFromPath(path, sink);
    }

    ref<SourceAccessor> getFSAccessor(bool requireValidPath) override
    {
        return LocalFSStore::getFSAccessor(requireValidPath);
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


ref<Store> MountedSSHStore::Config::openStore() const {
    ref config {shared_from_this()};

    if (config->mounted)
        return make_ref<MountedSSHStore>(config, *config->mounted);
    else
        return make_ref<SSHStore>(config);
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
    command.insert(command.end(),
        extraRemoteProgramArgs.begin(), extraRemoteProgramArgs.end());
    conn->sshConn = master.startCommand(std::move(command));
    conn->to = FdSink(conn->sshConn->in.get());
    conn->from = FdSource(conn->sshConn->out.get());
    return conn;
}

static RegisterStoreImplementation<SSHStore::Config> regSSHStore;
static RegisterStoreImplementation<MountedSSHStore::Config> regMountedSSHStore;

}
