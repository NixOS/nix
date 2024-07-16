#include "uds-remote-store.hh"
#include "unix-domain-socket.hh"
#include "worker-protocol.hh"
#include "worker-protocol-impl.hh"
#include "auth.hh"
#include "auth-tunnel.hh"

#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef _WIN32
# include <winsock2.h>
# include <afunix.h>
#else
# include <sys/socket.h>
# include <sys/un.h>
#endif

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
    std::string_view scheme,
    PathView socket_path,
    const Params & params)
    : UDSRemoteStore(params)
{
    if (!socket_path.empty())
        path.emplace(socket_path);
}


std::string UDSRemoteStore::getUri()
{
    if (path) {
        return std::string("unix://") + *path;
    } else {
        // unix:// with no path also works. Change what we return?
        return "daemon";
    }
}


void UDSRemoteStore::Connection::closeWrite()
{
    shutdown(toSocket(fd.get()), SHUT_WR);
}


ref<RemoteStore::Connection> UDSRemoteStore::openConnection()
{
    auto conn = make_ref<Connection>();

    /* Connect to a daemon that does the privileged work for us. */
    conn->fd = createUnixDomainSocket();

    nix::connect(toSocket(conn->fd.get()), path ? *path : settings.nixDaemonSocketFile);

    conn->from.fd = conn->fd.get();
    conn->to.fd = conn->fd.get();

    conn->startTime = std::chrono::steady_clock::now();

    return conn;
}


void UDSRemoteStore::initConnection(RemoteStore::Connection & _conn)
{
    Connection & conn(*(Connection *) &_conn);

    RemoteStore::initConnection(conn);

    if (GET_PROTOCOL_MINOR(conn.daemonVersion) >= 38
        && conn.remoteTrustsUs
        && experimentalFeatureSettings.isEnabled(Xp::AuthForwarding))
    {
        conn.authTunnel = std::make_unique<AuthTunnel>(
            *this, ((WorkerProto::ReadConn) _conn).version);

        conn.to << WorkerProto::Op::InitCallback;
        conn.to.flush();

        // Wait until the daemon is ready to receive the file
        // descriptor. This is so that the fd doesn't get lost in the
        // daemon's regular read() calls.
        readInt(conn.from);

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

        auto clientFd = std::move(conn.authTunnel->clientFd);

        *((int *) CMSG_DATA(cmsg)) = clientFd.get();

        msg.msg_controllen = CMSG_SPACE(sizeof(int));

        if (sendmsg(conn.fd.get(), &msg, 0) < 0)
            throw SysError("sending callback socket to the daemon");

        conn.processStderrReturn();
        readInt(conn.from);
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
