#include "auth-tunnel.hh"
#include "serialise.hh"
#include "auth.hh"
#include "store-api.hh"
#include "unix-domain-socket.hh"

#include <sys/socket.h>

namespace nix {

AuthTunnel::AuthTunnel(
    StoreDirConfig & storeConfig,
    WorkerProto::Version clientVersion)
    : clientVersion(clientVersion)
{
    auto sockets = socketPair();
    serverFd = std::move(sockets.first);
    clientFd = std::move(sockets.second);

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
                    debug("tunneling auth request: %s", authRequest);
                    // FIXME: handle exceptions
                    auto authData = auth::getAuthenticator()->fill(authRequest, required);
                    if (authData)
                        debug("tunneling auth response: %s", *authData);
                    to.to << 1;
                    WorkerProto::Serialise<std::optional<auth::AuthData>>::write(storeConfig, to, authData);
                    toSource.flush();
                    break;
                }

                case WorkerProto::CallbackOp::RejectAuth: {
                    auto authData = WorkerProto::Serialise<auth::AuthData>::read(storeConfig, from);
                    debug("tunneling auth data erase: %s", authData);
                    auth::getAuthenticator()->reject(authData);
                    to.to << 1;
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

    std::optional<auth::AuthData> get(const auth::AuthData & request, bool required) override
    {
        // FIXME: lock the connection
        to << (int) WorkerProto::CallbackOp::FillAuth;
        WorkerProto::Serialise<auth::AuthData>::write(*storeConfig, toConn, request);
        to << required;
        to.flush();

        if (readInt(from))
            return WorkerProto::Serialise<std::optional<auth::AuthData>>::read(*storeConfig, fromConn);
        else
            return std::nullopt;
    }

    void erase(const auth::AuthData & authData) override
    {
        // FIXME: lock the connection
        to << (int) WorkerProto::CallbackOp::RejectAuth;
        WorkerProto::Serialise<auth::AuthData>::write(*storeConfig, toConn, authData);
        to.flush();

        readInt(from);
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
