#include "nix/store/uds-remote-store.hh"
#include "nix/util/unix-domain-socket.hh"
#include "nix/store/worker-protocol.hh"
#include "nix/store/store-registration.hh"
#include "nix/store/globals.hh"

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

UDSRemoteStoreConfig::UDSRemoteStoreConfig(
    std::string_view scheme, std::string_view authority, const StoreReference::Params & params)
    : Store::Config{params}
    , LocalFSStore::Config{params}
    , RemoteStore::Config{params}
    , path{
          !authority.empty() ? std::string{authority} : getDefaultDaemonSocketPath().string(),
      }
{
    if (uriSchemes().count(scheme) == 0) {
        throw UsageError("Scheme must be 'unix'");
    }
}

std::string UDSRemoteStoreConfig::doc()
{
    return
#include "uds-remote-store.md"
        ;
}

// Empty authority will default to the per-store socket path based on
// stateDir.
UDSRemoteStoreConfig::UDSRemoteStoreConfig(const Params & params)
    : UDSRemoteStoreConfig(*uriSchemes().begin(), "", params)
{
}

UDSRemoteStore::UDSRemoteStore(ref<const Config> config)
    : Store{*config}
    , LocalFSStore{*config}
    , RemoteStore{*config}
    , config{config}
{
}

StoreReference UDSRemoteStoreConfig::getReference() const
{
    /* We specifically return "daemon" here instead of "unix://" or "unix://${path}"
     * to be more compatible with older versions of nix. Some tooling out there
     * tries hard to parse store references and it might not be able to handle "unix://". */
    if (path == getEnvNonEmpty("NIX_DAEMON_SOCKET_PATH").value_or(getDefaultDaemonSocketPath().string()))
        return {
            .variant = StoreReference::Daemon{},
            .params = getQueryParams(),
        };
    return {
        .variant =
            StoreReference::Specified{
                .scheme = *uriSchemes().begin(),
                .authority = path,
            },
        .params = getQueryParams(),
    };
}

void UDSRemoteStore::Connection::closeWrite()
{
    shutdown(toSocket(fd.get()), SHUT_WR);
}

ref<RemoteStore::Connection> UDSRemoteStore::openConnection()
{
    auto conn = make_ref<Connection>();

    /* Connect to a daemon that does the privileged work for us. */
    conn->fd = nix::connect(config->path);

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

ref<Store> UDSRemoteStore::Config::openStore() const
{
    return make_ref<UDSRemoteStore>(ref{shared_from_this()});
}

static RegisterStoreImplementation<UDSRemoteStore::Config> regUDSRemoteStore;

} // namespace nix
