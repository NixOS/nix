#include "nix/util/file-system.hh"
#include "nix/util/unix-domain-socket.hh"
#include "nix/util/util.hh"

#ifdef _WIN32
#  include <winsock2.h>
#  include <afunix.h>
#else
#  include <sys/socket.h>
#  include <sys/un.h>
#  include "nix/util/processes.hh"
#endif
#include <unistd.h>

namespace nix {

AutoCloseFD createUnixDomainSocket()
{
    AutoCloseFD fdSocket = toDescriptor(socket(
        PF_UNIX,
        SOCK_STREAM
#ifdef SOCK_CLOEXEC
            | SOCK_CLOEXEC
#endif
        ,
        0));
    if (!fdSocket)
        throw SysError("cannot create Unix domain socket");
#ifndef _WIN32
    unix::closeOnExec(fdSocket.get());
#endif
    return fdSocket;
}

AutoCloseFD createUnixDomainSocket(const std::filesystem::path & path, mode_t mode)
{
    auto fdSocket = nix::createUnixDomainSocket();

    bind(toSocket(fdSocket.get()), path);

    chmod(path, mode);

    if (listen(toSocket(fdSocket.get()), 100) == -1)
        throw SysError("cannot listen on socket %s", PathFmt(path));

    return fdSocket;
}

/**
 * Use string for path, because no `struct sockaddr_un` variant supports native wide character paths.
 */
static void
bindConnectProcHelper(std::string_view operationName, auto && operation, Socket fd, const std::string & path)
{
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;

    // Casting between types like these legacy C library interfaces
    // require is forbidden in C++. To maintain backwards
    // compatibility, the implementation of the bind/connect functions
    // contains some hints to the compiler that allow for this
    // special case.
    auto * psaddr = reinterpret_cast<struct sockaddr *>(&addr);

    if (path.size() + 1 >= sizeof(addr.sun_path)) {
#ifdef _WIN32
        throw Error("cannot %s to socket at '%s': path is too long", operationName, path);
#else
        Pipe pipe;
        pipe.create();
        Pid pid = startProcess([&] {
            try {
                pipe.readSide.close();
                auto dir = std::filesystem::path(path).parent_path();
                if (chdir(dir.string().c_str()) == -1)
                    throw SysError("chdir to %s failed", PathFmt(dir));
                std::string base(baseNameOf(path));
                if (base.size() + 1 >= sizeof(addr.sun_path))
                    throw Error("socket path '%s' is too long", base);
                memcpy(addr.sun_path, base.c_str(), base.size() + 1);
                if (operation(fd, psaddr, sizeof(addr)) == -1)
                    throw SysError("cannot %s to socket at '%s'", operationName, path);
                writeFull(pipe.writeSide.get(), "0\n");
            } catch (SysError & e) {
                writeFull(pipe.writeSide.get(), fmt("%d\n", e.errNo));
            } catch (...) {
                writeFull(pipe.writeSide.get(), "-1\n");
            }
        });
        pipe.writeSide.close();
        auto errNo = string2Int<int>(chomp(drainFD(pipe.readSide.get())));
        if (!errNo || *errNo == -1)
            throw Error("cannot %s to socket at '%s'", operationName, path);
        else if (*errNo > 0) {
            errno = *errNo;
            throw SysError("cannot %s to socket at '%s'", operationName, path);
        }
#endif
    } else {
        memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        if (operation(fd, psaddr, sizeof(addr)) == -1)
            throw SysError("cannot %s to socket at '%s'", operationName, path);
    }
}

void bind(Socket fd, const std::filesystem::path & path)
{
    tryUnlink(path);

    bindConnectProcHelper("bind", ::bind, fd, path.string());
}

void connect(Socket fd, const std::filesystem::path & path)
{
    bindConnectProcHelper("connect", ::connect, fd, path.string());
}

AutoCloseFD connect(const std::filesystem::path & path)
{
    auto fd = createUnixDomainSocket();
    nix::connect(toSocket(fd.get()), path);
    return fd;
}

} // namespace nix
