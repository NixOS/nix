#include "auth-tunnel.hh"
#include "serialise.hh"
#include "auth.hh"
#include "store-api.hh"

#include <sys/socket.h>

namespace nix {

AuthTunnel::AuthTunnel(
    StoreDirConfig & storeConfig,
    WorkerProto::Version clientVersion)
    : clientVersion(clientVersion)
{
    int sockets[2];
    if (socketpair(PF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets))
        throw SysError("creating a socket pair");

    serverFd = sockets[0];
    clientFd = sockets[1];

    serverThread = std::thread([this, clientVersion, &storeConfig]()
    {
        try {
            FdSource fromSource(serverFd.get());
            WorkerProto::ReadConn from {
                .from = fromSource,
                .version = clientVersion,
            };
            FdSink toSource(serverFd.get());
            WorkerProto::WriteConn to {
                .to = toSource,
                .version = clientVersion,
            };

            while (true) {
                auto op = (WorkerProto::CallbackOp) readInt(from.from);

                switch (op) {
                case WorkerProto::CallbackOp::FillAuth: {
                    auto authRequest = WorkerProto::Serialise<auth::AuthData>::read(storeConfig, from);
                    bool required;
                    from.from >> required;
                    printError("got auth request from daemon: %s", authRequest);
                    // FIXME: handle exceptions
                    auto authData = auth::getAuthenticator()->fill(authRequest, required);
                    if (authData)
                        printError("returning auth to daemon: %s", *authData);
                    to.to << 1;
                    WorkerProto::Serialise<std::optional<auth::AuthData>>::write(storeConfig, to, authData);
                    toSource.flush();
                    break;
                }

                default:
                    throw Error("invalid callback operation %1%", (int) op);
                }
            }
        } catch (EndOfFile &) {
        } catch (...) {
            ignoreException();
        }
    });
}

AuthTunnel::~AuthTunnel()
{
    if (serverFd)
        shutdown(serverFd.get(), SHUT_RDWR);

    if (serverThread.joinable())
        serverThread.join();
}

struct TunneledAuthSource : auth::AuthSource
{
    /**
     * File descriptor to send requests to the client.
     */
    AutoCloseFD fd;

    FdSource from;
    FdSink to;

    WorkerProto::ReadConn fromConn;
    WorkerProto::WriteConn toConn;

    ref<StoreDirConfig> storeConfig;

    TunneledAuthSource(
        ref<StoreDirConfig> storeConfig,
        WorkerProto::Version clientVersion,
        AutoCloseFD && fd)
        : fd(std::move(fd))
        , from(this->fd.get())
        , to(this->fd.get())
        , fromConn {.from = from, .version = clientVersion}
        , toConn {.to = to, .version = clientVersion}
        , storeConfig(storeConfig)
    { }

    std::optional<auth::AuthData> get(const auth::AuthData & request) override
    {
        // FIXME: lock the connection
        to << (int) WorkerProto::CallbackOp::FillAuth;
        WorkerProto::Serialise<auth::AuthData>::write(*storeConfig, toConn, request);
        to << false;
        to.flush();

        if (readInt(from))
            return WorkerProto::Serialise<std::optional<auth::AuthData>>::read(*storeConfig, fromConn);
        else
            return std::nullopt;
    }
};

ref<auth::AuthSource> makeTunneledAuthSource(
    ref<StoreDirConfig> storeConfig,
    WorkerProto::Version clientVersion,
    AutoCloseFD && clientFd)
{
    return make_ref<TunneledAuthSource>(storeConfig, clientVersion, std::move(clientFd));
}

}
