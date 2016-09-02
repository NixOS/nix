#include "ssh-store.hh"
#include "remote-fs-accessor.hh"
#include "archive.hh"
#include "worker-protocol.hh"
#include "pool.hh"

namespace nix {

SSHStore::SSHStore(string uri, const Params & params, size_t maxConnections)
    : Store(params)
    , RemoteStore(params, maxConnections)
    , tmpDir(createTempDir("", "nix", true, true, 0700))
    , socketPath((Path) tmpDir + "/ssh.sock")
    , sshMaster(startProcess([&]() {
                auto key = get(params, "ssh-key", "");
                if (key.empty())
                    execlp("ssh", "ssh", "-N", "-M", "-S", socketPath.c_str(), uri.c_str(), NULL);
                else
                    execlp("ssh", "ssh", "-N", "-M", "-S", socketPath.c_str(), "-i", key.c_str(), uri.c_str(), NULL);
                throw SysError("starting ssh master");
      }))
    , uri(std::move(uri))
{
}

string SSHStore::getUri()
{
    return "ssh://" + uri;
}

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
    Pipe in, out;
    in.create();
    out.create();
    conn->sshPid = startProcess([&]() {
        if (dup2(in.readSide.get(), STDIN_FILENO) == -1)
            throw SysError("duping over STDIN");
        if (dup2(out.writeSide.get(), STDOUT_FILENO) == -1)
            throw SysError("duping over STDOUT");
        execlp("ssh", "ssh", "-S", socketPath.c_str(), uri.c_str(), "nix-daemon", "--stdio", NULL);
        throw SysError("executing nix-daemon --stdio over ssh");
    });
    in.readSide = -1;
    out.writeSide = -1;
    conn->out = std::move(out.readSide);
    conn->in = std::move(in.writeSide);
    conn->to = FdSink(conn->in.get());
    conn->from = FdSource(conn->out.get());
    initConnection(*conn);
    return conn;
}

static RegisterStoreImplementation regStore([](
    const std::string & uri, const Store::Params & params)
    -> std::shared_ptr<Store>
{
    if (std::string(uri, 0, 6) != "ssh://") return 0;
    return std::make_shared<SSHStore>(uri.substr(6), params);
});

}
