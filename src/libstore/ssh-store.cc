#include "store-api.hh"
#include "remote-store.hh"
#include "remote-fs-accessor.hh"
#include "archive.hh"
#include "worker-protocol.hh"
#include "pool.hh"
#include "ssh.hh"

namespace nix {

struct SSHStoreConfig : virtual RemoteStoreConfig
{
    using RemoteStoreConfig::RemoteStoreConfig;

    const Setting<Path> sshKey{(StoreConfig*) this, "", "ssh-key", "path to an SSH private key"};
    const Setting<bool> compress{(StoreConfig*) this, false, "compress", "whether to compress the connection"};
    const Setting<Path> remoteProgram{(StoreConfig*) this, "nix-daemon", "remote-program", "path to the nix-daemon executable on the remote system"};
    const Setting<std::string> remoteStore{(StoreConfig*) this, "", "remote-store", "URI of the store on the remote system"};

    const std::string name() override { return "SSH Store"; }
};

class SSHStore : public virtual RemoteStore, public virtual SSHStoreConfig
{
public:

    SSHStore(const std::string & scheme, const std::string & host, const Params & params)
        : StoreConfig(params)
        , Store(params)
        , RemoteStore(params)
        , host(host)
        , master(
            host,
            sshKey,
            // Use SSH master only if using more than 1 connection.
            connections->capacity() > 1,
            compress)
    {
    }

    static std::set<std::string> uriSchemes() { return {"ssh-ng"}; }

    std::string getUri() override
    {
        return *uriSchemes().begin() + "://" + host;
    }

    bool sameMachine() override
    { return false; }

private:

    struct Connection : RemoteStore::Connection
    {
        std::unique_ptr<SSHMaster::Connection> sshConn;
    };

    ref<RemoteStore::Connection> openConnection() override;

    std::string host;

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

ref<RemoteStore::Connection> SSHStore::openConnection()
{
    auto conn = make_ref<Connection>();
    conn->sshConn = master.startCommand(
        fmt("%s --stdio", remoteProgram)
        + (remoteStore.get() == "" ? "" : " --store " + shellEscape(remoteStore.get())));
    conn->to = FdSink(conn->sshConn->in.get());
    conn->from = FdSource(conn->sshConn->out.get());
    return conn;
}

static RegisterStoreImplementation<SSHStore, SSHStoreConfig> regStore;

}
