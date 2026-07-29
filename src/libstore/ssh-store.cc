#include "nix/store/ssh-store.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/store/remote-store-connection.hh"
#include "nix/util/source-accessor.hh"
#include "nix/store/worker-protocol.hh"
#include "nix/store/worker-protocol-impl.hh"
#include "nix/util/pool.hh"
#include "nix/store/ssh.hh"
#include "nix/store/globals.hh"
#include "nix/store/store-registration.hh"

namespace nix {

SSHStoreConfig::SSHStoreConfig(const ParsedURL::Authority & authority, const Params & params)
    : Store::Config{params, FilePathType::Unix}
    , RemoteStore::Config{params, FilePathType::Unix}
    , CommonSSHStoreConfig{authority, params}
{
}

void SSHStoreConfig::anchor() {}

void MountedSSHStoreConfig::anchor() {}

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

struct alignas(8) /* Work around ASAN failures on i686-linux. */
    SSHStore : virtual RemoteStore
{
private:
    void anchor() override;

public:
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
    private:
        void anchor() override;

    public:
        std::unique_ptr<SSHMaster::Connection> sshConn;

        void closeWrite() override
        {
            sshConn->in.close();
        }
    };

    ref<RemoteStore::Connection> openConnection() override;

    std::vector<std::string> extraRemoteProgramArgs;

    SSHMaster master;

    void setOptions(RemoteStore::Connection & conn) override
    {
        /* Normally, we deliberately forward *no* settings to the
           remote side, since they are per-machine configuration and
           our local values would clobber the remote's own.

           TODO Add a way to explicitly ask for some options to be
           forwarded. One option: A way to query the daemon for its
           settings, and then a series of params to SSHStore like
           forward-cores or forward-overridden-cores that only
           override the requested settings.

           However, `keep-failed` is per-request rather than
           per-machine configuration, so the remote side should honor
           ours. (The legacy `nix-store --serve` protocol forwarded it
           with every build request.) The `SetOptions` message cannot
           express forwarding just one setting, so as a stop-gap hack,
           send it only when `keep-failed` is set, with an empty
           overrides map. Note that this also forwards our values of
           the other fixed fields of that message (verbosity,
           `max-jobs`, etc.) for such connections. */
        if (settings.keepFailed) {
            conn.to << WorkerProto::Op::SetOptions << settings.keepFailed << settings.getWorkerSettings().keepGoing
                    << settings.getWorkerSettings().tryFallback << verbosity
                    << settings.getWorkerSettings().maxBuildJobs << settings.getWorkerSettings().maxSilentTime << true
                    << (settings.verboseBuild ? lvlError : lvlVomit) << 0 // obsolete log type
                    << 0 /* obsolete print build trace */
                    << settings.getLocalSettings().buildCores << settings.getWorkerSettings().useSubstitutes
                    << 0 /* no overridden settings */;

            auto ex = conn.processStderrReturn();
            if (ex)
                std::rethrow_exception(ex);
        }
    };
};

void RemoteStore::Connection::anchor() {}

void SSHStore::Connection::anchor() {}

void SSHStore::anchor() {}

MountedSSHStoreConfig::MountedSSHStoreConfig(StringMap params)
    : StoreConfig(params, FilePathType::Native)
    , RemoteStoreConfig(params, FilePathType::Native)
    , CommonSSHStoreConfig(params)
    , SSHStoreConfig(params)
    , LocalFSStoreConfig(params)
{
}

MountedSSHStoreConfig::MountedSSHStoreConfig(const ParsedURL::Authority & authority, StringMap params)
    : StoreConfig(params, FilePathType::Native)
    , RemoteStoreConfig(params, FilePathType::Native)
    , CommonSSHStoreConfig(authority, params)
    , SSHStoreConfig(authority, params)
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
private:
    void anchor() override;

public:
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
    std::filesystem::path addPermRoot(const StorePath & path, const std::filesystem::path & gcRoot) override
    {
        auto conn(getConnection());
        conn->to << WorkerProto::Op::AddPermRoot;
        WorkerProto::write(*this, *conn, path);
        WorkerProto::write(*this, *conn, gcRoot.string());
        conn.processStderr();
        return readString(conn->from);
    }
};

void MountedSSHStore::anchor() {}

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
    conn->sshConn = master.startCommand(toOsStrings(std::move(command)));
    conn->to = FdSink(conn->sshConn->in.get());
    conn->from = FdSource(conn->sshConn->out.get());
    return conn;
}

static RegisterStoreImplementation<SSHStore::Config> regSSHStore;
static RegisterStoreImplementation<MountedSSHStore::Config> regMountedSSHStore;

} // namespace nix
