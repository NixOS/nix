#include "nix/util/json-utils.hh"
#include "nix/store/ssh-store.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/store/remote-store-connection.hh"
#include "nix/util/source-accessor.hh"
#include "nix/util/archive.hh"
#include "nix/store/worker-protocol.hh"
#include "nix/store/worker-protocol-impl.hh"
#include "nix/util/pool.hh"
#include "nix/store/ssh.hh"
#include "nix/store/config-parse-impl.hh"
#include "nix/store/store-registration.hh"

namespace nix {

constexpr static const SSHStoreConfigT<config::SettingInfoWithDefault> sshStoreConfigDescriptions = {
    .remoteProgram{
        {
            .name = "remote-program",
            .description = "Path to the `nix-daemon` executable on the remote machine.",
        },
        {
            .makeDefault = []() -> Strings { return {"nix-daemon"}; },
        },
    },
};

#define SSH_STORE_CONFIG_FIELDS(X) X(remoteProgram)

MAKE_PARSE(SSHStoreConfig, sshStoreConfig, SSH_STORE_CONFIG_FIELDS)

MAKE_APPLY_PARSE(SSHStoreConfig, sshStoreConfig, SSH_STORE_CONFIG_FIELDS)

config::SettingDescriptionMap SSHStoreConfig::descriptions()
{
    config::SettingDescriptionMap ret;
    ret.merge(StoreConfig::descriptions());
    ret.merge(CommonSSHStoreConfig::descriptions());
    ret.merge(RemoteStoreConfig::descriptions());
    {
        constexpr auto & descriptions = sshStoreConfigDescriptions;
        ret.merge(decltype(ret){SSH_STORE_CONFIG_FIELDS(DESCRIBE_ROW)});
    }
    ret.insert_or_assign(
        "mounted",
        config::SettingDescription{
            .description = stripIndentation(R"(
                If this nested settings object is defined (`{..}` not `null`), additionally requires that store be mounted in the local file system.

                The mounting of that store is not managed by Nix, and must by managed manually.
                It could be accomplished with SSHFS or NFS, for example.

                The local file system is used to optimize certain operations.
                For example, rather than serializing Nix archives and sending over the Nix channel,
                we can directly access the file system data via the mount-point.

                The local file system is also used to make certain operations possible that wouldn't otherwise be.
                For example, persistent GC roots can be created if they reside on the same file system as the remote store:
                the remote side will create the symlinks necessary to avoid race conditions.
            )"),
            .experimentalFeature = Xp::MountedSSHStore,
            .info = config::SettingDescription::Sub{.nullable = true, .map = LocalFSStoreConfig::descriptions()},
        });
    return ret;
}

static std::optional<LocalFSStore::Config> getMounted(
    const Store::Config & storeConfig,
    const StoreConfig::Params & params,
    const ExperimentalFeatureSettings & xpSettings)
{
    auto mountedParamsOpt = optionalValueAt(params, "mounted");
    if (!mountedParamsOpt)
        return {};
    auto * mountedParamsP = getNullable(*mountedParamsOpt);
    xpSettings.require(Xp::MountedSSHStore);
    if (!mountedParamsP)
        return {};
    auto & mountedParams = getObject(*mountedParamsP);
    return {{storeConfig, mountedParams}};
}

SSHStoreConfig::SSHStoreConfig(
    std::string_view scheme,
    std::string_view authority,
    const StoreConfig::Params & params,
    const ExperimentalFeatureSettings & xpSettings)
    : Store::Config{params}
    , RemoteStore::Config{*this, params}
    , CommonSSHStoreConfig{scheme, authority, params}
    , SSHStoreConfigT<config::PlainValue>{sshStoreConfigApplyParse(params)}
    , mounted{getMounted(*this, params, xpSettings)}
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

ref<Store> MountedSSHStore::Config::openStore() const
{
    ref config{shared_from_this()};

    if (config->mounted)
        return make_ref<MountedSSHStore>(config, *config->mounted);
    else
        return make_ref<SSHStore>(config);
}

ref<RemoteStore::Connection> SSHStore::openConnection()
{
    auto conn = make_ref<Connection>();
    Strings command = config->remoteProgram;
    command.push_back("--stdio");
    if (config->remoteStore != "") {
        command.push_back("--store");
        command.push_back(config->remoteStore);
    }
    command.insert(command.end(), extraRemoteProgramArgs.begin(), extraRemoteProgramArgs.end());
    conn->sshConn = master.startCommand(std::move(command));
    conn->to = FdSink(conn->sshConn->in.get());
    conn->from = FdSource(conn->sshConn->out.get());
    return conn;
}

static RegisterStoreImplementation<SSHStore::Config> regSSHStore;

} // namespace nix
