#include "store-api.hh"
#include "remote-store.hh"
#include "remote-fs-accessor.hh"
#include "archive.hh"
#include "worker-protocol.hh"
#include "pool.hh"
#include "ssh.hh"

namespace nix {

static std::string uriScheme = "ssh-ng://";

class SSHStore : public RemoteStore
{
public:

    SSHStore(const std::string & host, const Params & params)
        : Store(params)
        , RemoteStore(params)
        , host(host)
        , master(
            host,
            get(params, "ssh-key", ""),
            // Use SSH master only if using more than 1 connection.
            connections->capacity() > 1,
            get(params, "compress", "") == "true")
    {
    }

    std::string getUri() override
    {
        return uriScheme + host;
    }

    void narFromPath(const Path & path, Sink & sink) override;

    ref<FSAccessor> getFSAccessor() override;

private:

    struct Connection : RemoteStore::Connection
    {
        std::unique_ptr<SSHMaster::Connection> sshConn;
    };

    ref<RemoteStore::Connection> openConnection() override;

    std::string host;

    SSHMaster master;
};


class ForwardSource : public Source
{
    Source & readSource;
    Sink & writeSink;
public:
    ForwardSource(Source & readSource, Sink & writeSink) : readSource(readSource), writeSink(writeSink) {}
    size_t read(unsigned char * data, size_t len) override
    {
        auto res = readSource.read(data, len);
        writeSink(data, len);
        return res;
    }
};

void SSHStore::narFromPath(const Path & path, Sink & sink)
{
    auto conn(connections->get());
    conn->to << wopNarFromPath << path;
    conn->processStderr();
    ParseSink ps;
    auto fwd = ForwardSource(conn->from, sink);
    parseDump(ps, fwd);
}

ref<FSAccessor> SSHStore::getFSAccessor()
{
    return make_ref<RemoteFSAccessor>(ref<Store>(shared_from_this()));
}

ref<RemoteStore::Connection> SSHStore::openConnection()
{
    auto conn = make_ref<Connection>();
    conn->sshConn = master.startCommand("nix-daemon --stdio");
    conn->to = FdSink(conn->sshConn->in.get());
    conn->from = FdSource(conn->sshConn->out.get());
    initConnection(*conn);
    return conn;
}

static RegisterStoreImplementation regStore([](
    const std::string & uri, const Store::Params & params)
    -> std::shared_ptr<Store>
{
    if (std::string(uri, 0, uriScheme.size()) != uriScheme) return 0;
    return std::make_shared<SSHStore>(std::string(uri, uriScheme.size()), params);
});

}
