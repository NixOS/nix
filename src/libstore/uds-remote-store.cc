#include "uds-remote-store.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>


namespace nix {

UDSRemoteStore::UDSRemoteStore(const Params & params)
    : StoreConfig(params)
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


ref<RemoteStore::Connection> UDSRemoteStore::openConnection()
{
    auto conn = make_ref<Connection>();

    /* Connect to a daemon that does the privileged work for us. */
    conn->fd = socket(PF_UNIX, SOCK_STREAM
        #ifdef SOCK_CLOEXEC
        | SOCK_CLOEXEC
        #endif
        , 0);
    if (!conn->fd)
        throw SysError("cannot create Unix domain socket");
    closeOnExec(conn->fd.get());

    string socketPath = path ? *path : settings.nixDaemonSocketFile;

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    if (socketPath.size() + 1 >= sizeof(addr.sun_path))
        throw Error("socket path '%1%' is too long", socketPath);
    strcpy(addr.sun_path, socketPath.c_str());

    if (::connect(conn->fd.get(), (struct sockaddr *) &addr, sizeof(addr)) == -1)
        throw SysError("cannot connect to daemon at '%1%'", socketPath);

    conn->from.fd = conn->fd.get();
    conn->to.fd = conn->fd.get();

    conn->startTime = std::chrono::steady_clock::now();

    return conn;
}


static RegisterStoreImplementation<UDSRemoteStore, UDSRemoteStoreConfig> regUDSRemoteStore;

}
