#include "uds-remote-store.hh"
#include "unix-domain-socket.hh"
#include "worker-protocol.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <afunix.h>
#else
#  include <sys/socket.h>
#  include <sys/un.h>
#endif

namespace nix {

std::string UDSRemoteStoreConfig::doc()
{
    return
#include "uds-remote-store.md"
        ;
}

UDSRemoteStore::UDSRemoteStore(std::string_view scheme, std::string_view authority, const Params & params)
    : StoreConfig(params)
    , LocalFSStoreConfig(params)
    , RemoteStoreConfig(params)
    , UDSRemoteStoreConfig(scheme, authority, params)
    , Store(params)
    , LocalFSStore(params)
    , RemoteStore(params)
{
}

std::string UDSRemoteStore::getPathOrDefault() const
{
    return path.value_or(settings.nixDaemonSocketFile);
}

std::string UDSRemoteStore::getUri()
{
    return std::format("{}://{}", UNIX_SCHEME, getPathOrDefault());
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

    nix::connect(toSocket(conn->fd.get()), getPathOrDefault());

    conn->from.fd = conn->fd.get();
    conn->to.fd = conn->fd.get();

    conn->startTime = std::chrono::steady_clock::now();

    return conn;
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
