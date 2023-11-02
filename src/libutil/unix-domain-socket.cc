#include "file-system.hh"
#include "processes.hh"
#include "unix-domain-socket.hh"
#include "util.hh"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace nix {

AutoCloseFD createUnixDomainSocket()
{
    AutoCloseFD fdSocket = socket(PF_UNIX, SOCK_STREAM
        #ifdef SOCK_CLOEXEC
        | SOCK_CLOEXEC
        #endif
        , 0);
    if (!fdSocket)
        throw SysError("cannot create Unix domain socket");
    closeOnExec(fdSocket.get());
    return fdSocket;
}


AutoCloseFD createUnixDomainSocket(const Path & path, mode_t mode)
{
    auto fdSocket = nix::createUnixDomainSocket();

    bind(fdSocket.get(), path);

    if (chmod(path.c_str(), mode) == -1)
        throw SysError("changing permissions on '%1%'", path);

    if (listen(fdSocket.get(), 100) == -1)
        throw SysError("cannot listen on socket '%1%'", path);

    return fdSocket;
}


void bind(int fd, const std::string & path)
{
    unlink(path.c_str());

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;

    if (path.size() + 1 >= sizeof(addr.sun_path)) {
        Pid pid = startProcess([&] {
            Path dir = dirOf(path);
            if (chdir(dir.c_str()) == -1)
                throw SysError("chdir to '%s' failed", dir);
            std::string base(baseNameOf(path));
            if (base.size() + 1 >= sizeof(addr.sun_path))
                throw Error("socket path '%s' is too long", base);
            memcpy(addr.sun_path, base.c_str(), base.size() + 1);
            if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1)
                throw SysError("cannot bind to socket '%s'", path);
            _exit(0);
        });
        int status = pid.wait();
        if (status != 0)
            throw Error("cannot bind to socket '%s'", path);
    } else {
        memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1)
            throw SysError("cannot bind to socket '%s'", path);
    }
}


void connect(int fd, const std::string & path)
{
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;

    if (path.size() + 1 >= sizeof(addr.sun_path)) {
        Pipe pipe;
        pipe.create();
        Pid pid = startProcess([&] {
            try {
                pipe.readSide.close();
                Path dir = dirOf(path);
                if (chdir(dir.c_str()) == -1)
                    throw SysError("chdir to '%s' failed", dir);
                std::string base(baseNameOf(path));
                if (base.size() + 1 >= sizeof(addr.sun_path))
                    throw Error("socket path '%s' is too long", base);
                memcpy(addr.sun_path, base.c_str(), base.size() + 1);
                if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1)
                    throw SysError("cannot connect to socket at '%s'", path);
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
            throw Error("cannot connect to socket at '%s'", path);
        else if (*errNo > 0) {
            errno = *errNo;
            throw SysError("cannot connect to socket at '%s'", path);
        }
    } else {
        memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1)
            throw SysError("cannot connect to socket at '%s'", path);
    }
}

}
