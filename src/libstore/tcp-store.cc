#include "remote-store.hh"
#include "finally.hh"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

namespace nix {

struct TCPStoreConfig : virtual RemoteStoreConfig
{
    using RemoteStoreConfig::RemoteStoreConfig;

    const std::string name() override { return "TCP Store"; }
};

struct TCPStore : public virtual TCPStoreConfig, public virtual RemoteStore
{
    std::string host;
    uint16_t port = 0;

    TCPStore(const std::string scheme, std::string path, const Params & params)
        : StoreConfig(params)
        , RemoteStoreConfig(params)
        , TCPStoreConfig(params)
        , Store(params)
        , RemoteStore(params)
    {
        // FIXME: use parsed URL.

        auto p = path.find(':');
        if (p == std::string::npos)
            throw UsageError("tcp:// stores require a port number (e.g. 'tcp://example.org:1234'), in '%s'", path);

        host = std::string(path, 0, p);
        if (auto port2 = string2Int<uint16_t>(std::string(path, p + 1)))
            port = *port2;
        else
            throw UsageError("invalid TCP port number, in '%s'", path);
    }

    std::string getUri() override
    { return fmt("tcp://"); }

    static std::set<std::string> uriSchemes()
    { return {"tcp"}; }

    bool sameMachine() override
    { return false; }

    ref<RemoteStore::Connection> openConnection() override
    {
        auto conn = make_ref<Connection>();

        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = 0;
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo * result;
        if (auto s = getaddrinfo(host.c_str(), fmt("%d", port).c_str(), &hints, &result))
            throw Error("DNS lookup of '%s' failed: %s", host, gai_strerror(s));

        Finally cleanup([&]() { freeaddrinfo(result); });

        std::string err;

        for (auto rp = result; rp; rp = rp->ai_next) {
            AutoCloseFD fd = socket(
                rp->ai_family,
                rp->ai_socktype
                #ifdef SOCK_CLOEXEC
                | SOCK_CLOEXEC
                #endif
                , rp->ai_protocol);
            if (!fd) {
                err = strerror(errno);
                continue;
            }
            closeOnExec(fd.get());

            if (::connect(fd.get(), rp->ai_addr, rp->ai_addrlen) == -1) {
                err = strerror(errno);
                continue;
            }

            conn->fd = std::move(fd);
            conn->from.fd = conn->fd.get();
            conn->to.fd = conn->fd.get();
            conn->startTime = std::chrono::steady_clock::now();
            return conn;
        }

        throw Error("could not connect to daemon at '%s': %s", host, err);
    }
};

static RegisterStoreImplementation<TCPStore, TCPStoreConfig> regTCPStore;

}
