///@file

#include "nix/cmd/unix-socket-server.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/file-system.hh"
#include "nix/util/logging.hh"
#include "nix/util/signals.hh"
#include "nix/util/strings.hh"
#include "nix/util/unix-domain-socket.hh"
#include "nix/util/util.hh"

#include <sys/types.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <afunix.h>
#else
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <poll.h>
#endif

#if defined(__APPLE__) || defined(__FreeBSD__)
#  include <sys/ucred.h>
#endif

namespace nix {

#ifndef _WIN32
namespace unix {

PeerInfo getPeerInfo(Descriptor remote)
{
    PeerInfo peer;

#  if defined(SO_PEERCRED)

#    if defined(__OpenBSD__)
    struct sockpeercred cred;
#    else
    ucred cred;
#    endif
    socklen_t credLen = sizeof(cred);
    if (getsockopt(remote, SOL_SOCKET, SO_PEERCRED, &cred, &credLen) == 0) {
        peer.pid = cred.pid;
        peer.uid = cred.uid;
        peer.gid = cred.gid;
    }

#  elif defined(LOCAL_PEERCRED)

#    if !defined(SOL_LOCAL)
#      define SOL_LOCAL 0
#    endif

    xucred cred;
    socklen_t credLen = sizeof(cred);
    if (getsockopt(remote, SOL_LOCAL, LOCAL_PEERCRED, &cred, &credLen) == 0)
        peer.uid = cred.cr_uid;

#  endif

    return peer;
}

} // namespace unix
#endif

[[noreturn]] void serveUnixSocket(const ServeUnixSocketOptions & options, UnixSocketHandler handler)
{
    std::vector<AutoCloseFD> listeningSockets;

#ifndef _WIN32
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
            unix::closeOnExec(fdSocket.get());
            listeningSockets.push_back(std::move(fdSocket));
        }
    }

    //  Otherwise, create and bind to a Unix domain socket.
    else {
#else
    {
#endif
        createDirs(options.socketPath.parent_path());
        listeningSockets.push_back(createUnixDomainSocket(options.socketPath.string(), options.socketMode));
    }

#ifndef _WIN32
    std::vector<struct pollfd> fds;
    for (auto & i : listeningSockets)
        fds.push_back({.fd = i.get(), .events = POLLIN});
#endif

    //  Loop accepting connections.
    while (1) {
        try {
            checkInterrupt();

#ifndef _WIN32
            auto count = poll(fds.data(), fds.size(), -1);
            if (count == -1) {
                if (errno == EINTR)
                    continue;
                throw SysError("polling for incoming connections");
            }

            for (auto & pollfd : fds) {
                if (!pollfd.revents)
                    continue;
                Socket fd = toSocket(pollfd.fd);
#else
            assert(listeningSockets.size() == 1);
            {
                Socket fd = toSocket(listeningSockets[0].get());
#endif

                // Accept a connection.
                struct sockaddr_un remoteAddr;
#ifndef _WIN32
                socklen_t
#else
                int
#endif
                    remoteAddrLen = sizeof(remoteAddr);

                AutoCloseFD remote = fromSocket(accept(fd, (struct sockaddr *) &remoteAddr, &remoteAddrLen));
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

} // namespace nix
