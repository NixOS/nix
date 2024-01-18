#include "uds-remote-store.hh"
#include "unix-domain-socket.hh"
#include "worker-protocol.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>


namespace nix {

std::string UnmountedUDSRemoteStoreConfig::doc()
{
    return
        #include "unmounted-uds-remote-store.md"
        ;
}


std::string UDSRemoteStoreConfig::doc()
{
    return
        #include "uds-remote-store.md"
        ;
}


UnmountedUDSRemoteStore::UnmountedUDSRemoteStore(const Params & params)
    : StoreConfig(params)
    , RemoteStoreConfig(params)
    , UnmountedUDSRemoteStoreConfig(params)
    , Store(params)
    , RemoteStore(params)
{
}


UDSRemoteStore::UDSRemoteStore(const Params & params)
    : StoreConfig(params)
    , LocalFSStoreConfig(params)
    , RemoteStoreConfig(params)
    , UnmountedUDSRemoteStoreConfig(params)
    , UDSRemoteStoreConfig(params)
    , Store(params)
    , LocalFSStore(params)
    , RemoteStore(params)
    , UnmountedUDSRemoteStore(params)
{
}


UnmountedUDSRemoteStore::UnmountedUDSRemoteStore(
    const std::string scheme,
    std::string socket_path,
    const Params & params)
    : UnmountedUDSRemoteStore(params)
{
    path.emplace(socket_path);
}


UDSRemoteStore::UDSRemoteStore(
    const std::string scheme,
    std::string socket_path,
    const Params & params)
    : UDSRemoteStore(params)
{
    path.emplace(socket_path);
}


std::string UnmountedUDSRemoteStore::getUri()
{
    if (path) {
        return *uriSchemes().begin() + "://" + *path;
    } else {
        return "daemon";
    }
}


void UnmountedUDSRemoteStore::Connection::closeWrite()
{
    shutdown(fd.get(), SHUT_WR);
}


ref<RemoteStore::Connection> UnmountedUDSRemoteStore::openConnection()
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


void UDSRemoteStore::addIndirectRoot(const Path & path)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::AddIndirectRoot << path;
    conn.processStderr();
    readInt(conn->from);
}


static RegisterStoreImplementation<UnmountedUDSRemoteStore, UnmountedUDSRemoteStoreConfig> regUnmountedUDSRemoteStore;
static RegisterStoreImplementation<UDSRemoteStore, UDSRemoteStoreConfig> regUDSRemoteStore;

}
