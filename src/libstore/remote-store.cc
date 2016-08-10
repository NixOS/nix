#include "remote-store.hh"

#include <sys/socket.h>
#include <sys/un.h>


namespace nix {

RemoteStore::RemoteStore(const Params & params, size_t maxConnections)
    : Store(params)
    , LocalFSStore(params)
    , DaemonStore(params, maxConnections)
{
}


std::string RemoteStore::getUri()
{
    return "daemon";
}


ref<DaemonStore::Connection> RemoteStore::openConnection()
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

    string socketPath = settings.nixDaemonSocketFile;

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    if (socketPath.size() + 1 >= sizeof(addr.sun_path))
        throw Error(format("socket path ‘%1%’ is too long") % socketPath);
    strcpy(addr.sun_path, socketPath.c_str());

    if (connect(conn->fd.get(), (struct sockaddr *) &addr, sizeof(addr)) == -1)
        throw SysError(format("cannot connect to daemon at ‘%1%’") % socketPath);

    conn->from.fd = conn->fd.get();
    conn->to.fd = conn->fd.get();

    initConnection(*conn);

    return conn;
}

}
