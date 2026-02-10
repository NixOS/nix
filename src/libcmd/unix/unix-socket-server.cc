///@file

#include "nix/cmd/unix-socket-server.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/file-system.hh"
#include "nix/util/logging.hh"
#include "nix/util/signals.hh"
#include "nix/util/strings.hh"
#include "nix/util/unix-domain-socket.hh"
#include "nix/util/util.hh"

#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

#if defined(__APPLE__) || defined(__FreeBSD__)
#  include <sys/ucred.h>
#endif

namespace nix::unix {

PeerInfo getPeerInfo(Descriptor remote)
{
    PeerInfo peer;

#if defined(SO_PEERCRED)

#  if defined(__OpenBSD__)
    struct sockpeercred cred;
#  else
    ucred cred;
#  endif
    socklen_t credLen = sizeof(cred);
    if (getsockopt(remote, SOL_SOCKET, SO_PEERCRED, &cred, &credLen) == 0) {
        peer.pid = cred.pid;
        peer.uid = cred.uid;
        peer.gid = cred.gid;
    }

#elif defined(LOCAL_PEERCRED)

#  if !defined(SOL_LOCAL)
#    define SOL_LOCAL 0
#  endif

    xucred cred;
    socklen_t credLen = sizeof(cred);
    if (getsockopt(remote, SOL_LOCAL, LOCAL_PEERCRED, &cred, &credLen) == 0)
        peer.uid = cred.cr_uid;

#endif

    return peer;
}

[[noreturn]] void serveUnixSocket(const ServeUnixSocketOptions & options, UnixSocketHandler handler)
{
    std::vector<AutoCloseFD> listeningSockets;

    static constexpr int SD_LISTEN_FDS_START = 3;

    //  Handle socket-based activation by systemd.
    auto listenFds = getEnv("LISTEN_FDS");
    if (listenFds) {
        if (getEnv("LISTEN_PID") != std::to_string(getpid()))
            throw Error("unexpected systemd environment variables");
        auto count = string2Int<unsigned int>(*listenFds);
        assert(count);
        for (unsigned int i = 0; i < count; ++i) {
            AutoCloseFD fdSocket(SD_LISTEN_FDS_START + i);
            closeOnExec(fdSocket.get());
            listeningSockets.push_back(std::move(fdSocket));
        }
    }

    //  Otherwise, create and bind to a Unix domain socket.
    else {
        createDirs(options.socketPath.parent_path());
        listeningSockets.push_back(createUnixDomainSocket(options.socketPath.string(), options.socketMode));
    }

    std::vector<struct pollfd> fds;
    for (auto & i : listeningSockets)
        fds.push_back({.fd = i.get(), .events = POLLIN});

    //  Loop accepting connections.
    while (1) {
        try {
            checkInterrupt();

            auto count = poll(fds.data(), fds.size(), -1);
            if (count == -1) {
                if (errno == EINTR)
                    continue;
                throw SysError("polling for incoming connections");
            }

            for (auto & fd : fds) {
                if (!fd.revents)
                    continue;

                // Accept a connection.
                struct sockaddr_un remoteAddr;
                socklen_t remoteAddrLen = sizeof(remoteAddr);

                AutoCloseFD remote = accept(fd.fd, (struct sockaddr *) &remoteAddr, &remoteAddrLen);
                checkInterrupt();
                if (!remote) {
                    if (errno == EINTR)
                        continue;
                    throw SysError("accepting connection");
                }

                handler(std::move(remote), [&]() { listeningSockets.clear(); });
            }

        } catch (Error & error) {
            auto ei = error.info();
            // FIXME: add to trace?
            ei.msg = HintFmt("while processing connection: %1%", ei.msg.str());
            logError(ei);
        }
    }
}

} // namespace nix::unix
