#include "uds-remote-store.hh"
#include "unix-domain-socket.hh"
#include "worker-protocol.hh"
#include "worker-protocol-impl.hh"
#include "auth.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>


namespace nix {

std::string UDSRemoteStoreConfig::doc()
{
    return
        #include "uds-remote-store.md"
        ;
}


UDSRemoteStore::UDSRemoteStore(const Params & params)
    : StoreConfig(params)
    , LocalFSStoreConfig(params)
    , RemoteStoreConfig(params)
    , UDSRemoteStoreConfig(params)
    , Store(params)
    , LocalFSStore(params)
    , RemoteStore(params)
{
}


UDSRemoteStore::UDSRemoteStore(
    const std::string scheme,
    std::string socket_path,
    const Params & params)
    : UDSRemoteStore(params)
{
    path.emplace(socket_path);
}


std::string UDSRemoteStore::getUri()
{
    if (path) {
        return std::string("unix://") + *path;
    } else {
        return "daemon";
    }
}


UDSRemoteStore::Connection::~Connection()
{
    if (callbackFd)
        shutdown(callbackFd.get(), SHUT_RDWR);

    if (callbackThread.joinable())
        callbackThread.join();
}


void UDSRemoteStore::Connection::closeWrite()
{
    shutdown(fd.get(), SHUT_WR);
}


ref<RemoteStore::Connection> UDSRemoteStore::openConnection()
{
    auto conn = make_ref<Connection>();

    /* Connect to a daemon that does the privileged work for us. */
    conn->fd = createUnixDomainSocket();

    nix::connect(conn->fd.get(), path ? *path : settings.nixDaemonSocketFile);

    conn->from.fd = conn->fd.get();
    conn->to.fd = conn->fd.get();

    conn->startTime = std::chrono::steady_clock::now();

    return conn;
}


void UDSRemoteStore::initConnection(RemoteStore::Connection & _conn)
{
    Connection & conn(*(Connection *) &_conn);

    RemoteStore::initConnection(conn);

    if (GET_PROTOCOL_MINOR(conn.daemonVersion) >= 38 && conn.remoteTrustsUs) {
        int sockets[2];
        if (socketpair(PF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets))
            throw SysError("creating a socket pair");

        conn.callbackFd = sockets[0];
        AutoCloseFD otherSide = sockets[1];

        conn.callbackThread = std::thread([this, &conn, clientVersion{((WorkerProto::ReadConn) _conn).version}]()
        {
            try {
                FdSource fromSource(conn.callbackFd.get());
                WorkerProto::ReadConn from {
                    .from = fromSource,
                    .version = clientVersion,
                };
                FdSink toSource(conn.callbackFd.get());
                WorkerProto::WriteConn to {
                    .to = toSource,
                    .version = clientVersion,
                };

                while (true) {
                    auto op = (WorkerProto::CallbackOp) readInt(from.from);

                    switch (op) {
                    case WorkerProto::CallbackOp::FillAuth: {
                        auto authRequest = WorkerProto::Serialise<auth::AuthData>::read(*this, from);
                        bool required;
                        from.from >> required;
                        printError("got auth request from daemon: %s", authRequest);
                        // FIXME: handle exceptions
                        auto authData = auth::getAuthenticator()->fill(authRequest, required);
                        if (authData)
                            printError("returning auth to daemon: %s", *authData);
                        to.to << 1;
                        WorkerProto::Serialise<std::optional<auth::AuthData>>::write(*this, to, authData);
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

        conn.to << WorkerProto::Op::InitCallback;
        conn.to.flush();

        conn.processStderr();
        readInt(conn.from);

        // FIXME: do this before processStderr().
        struct msghdr msg = { 0 };
        char buf[CMSG_SPACE(sizeof(int))];
        memset(buf, '\0', sizeof(buf));
        struct iovec io = { .iov_base = (void *) "xy", .iov_len = 2 };

        msg.msg_iov = &io;
        msg.msg_iovlen = 1;
        msg.msg_control = buf;
        msg.msg_controllen = sizeof(buf);

        struct cmsghdr * cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));

        *((int *) CMSG_DATA(cmsg)) = otherSide.get();

        msg.msg_controllen = CMSG_SPACE(sizeof(int));

        if (sendmsg(conn.fd.get(), &msg, 0) < 0)
            throw SysError("sending callback socket to the daemon");
    }
}


void UDSRemoteStore::addIndirectRoot(const Path & path)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::AddIndirectRoot << path;
    conn.processStderr();
    readInt(conn->from);
}


static RegisterStoreImplementation<UDSRemoteStore, UDSRemoteStoreConfig> regUDSRemoteStore;

}
