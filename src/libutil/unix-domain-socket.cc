#include "nix/util/file-system.hh"
#include "nix/util/unix-domain-socket.hh"
#include "nix/util/util.hh"

#include <memory>
#include <span>

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

AutoCloseFD createUnixDomainSocket(const Path & path, mode_t mode)
{
    auto fdSocket = nix::createUnixDomainSocket();

    bind(fdSocket.get(), path);

    chmod(path, mode);

    if (listen(toSocket(fdSocket.get()), 100) == -1)
        throw SysError("cannot listen on socket '%1%'", path);

    return fdSocket;
}

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
                Path dir = dirOf(path);
                if (chdir(dir.c_str()) == -1)
                    throw SysError("chdir to '%s' failed", dir);
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

void bind(Socket fd, const std::string & path)
{
    unlink(path.c_str());

    bindConnectProcHelper("bind", ::bind, fd, path);
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

#ifndef _WIN32

void unix::sendMessageWithFds(Descriptor sockfd, std::string_view data, std::span<const Descriptor> fds)
{
    struct iovec iov{
        .iov_base = const_cast<char *>(data.data()),
        .iov_len = data.size(),
    };

    struct msghdr msg{
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = nullptr,
        .msg_controllen = 0,
    };

    auto cmsghdrAlign = std::align_val_t{alignof(struct cmsghdr)};

    auto deleteWrapper = [&](void * ptr) { ::operator delete(ptr, cmsghdrAlign); };

    // Allocate control message buffer with proper alignment for struct cmsghdr
    std::unique_ptr<void, decltype(deleteWrapper)> controlData(nullptr, deleteWrapper);

    if (!fds.empty()) {
        size_t controlSize = CMSG_SPACE(sizeof(int) * fds.size());
        controlData.reset(::operator new(controlSize, cmsghdrAlign));

        msg.msg_control = controlData.get();
        msg.msg_controllen = static_cast<socklen_t>(controlSize);

        auto * cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * fds.size());
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;

        // Copy file descriptors into the control message.
        // sendmsg() copies them into the kernel, so the caller retains
        // ownership and no dup() is needed.
        auto * fdPtr = reinterpret_cast<int *>(CMSG_DATA(cmsg));
        for (size_t i = 0; i < fds.size(); ++i) {
            fdPtr[i] = fds[i];
        }
    }

    // Loop to handle partial writes. Ancillary data (FDs) is only
    // sent with the first successful sendmsg call.
    while (iov.iov_len > 0) {
        ssize_t sent = sendmsg(sockfd, &msg, 0);
        if (sent < 0)
            throw SysError("sendmsg");

        iov.iov_base = static_cast<char *>(iov.iov_base) + sent;
        iov.iov_len -= sent;

        // Clear ancillary data after first successful send — the kernel
        // only delivers it once.
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
    }
}

unix::ReceivedMessage unix::receiveMessageWithFds(Descriptor sockfd, std::span<std::byte> data)
{
    static constexpr size_t maxFds =
#  ifdef __linux__
        // `SCM_MAX_FD` (defined in kernel `net/scm.h`, not exposed to
        // userspace) limits `SCM_RIGHTS` to 253 FDs per message.
        253
#  else
        // Darwin:
        // https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/uipc_usrreq.c#L121
        // FreeBSD: unknown.
        512
#  endif
        ;

    /* We create a buffer large enough (we think) to hold the maximum
       number of file descriptors that can be sent in a single message.

       This relates to the fact that while the data may be
       stream-oriented (and thus we can read much or as little as we
       want at our leisure, the file descriptors are message oriented,
       and if we fail to read some file descriptors that were sent as
       part of a message, they will be lost forever. */
    alignas(struct cmsghdr) std::byte controlBuf[CMSG_SPACE(sizeof(int) * maxFds)];

    struct iovec iov{
        .iov_base = data.data(),
        .iov_len = data.size(),
    };

    struct msghdr msg{
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = controlBuf,
        .msg_controllen = sizeof(controlBuf),
    };

    int flags = 0;
#  ifdef MSG_CMSG_CLOEXEC
    flags |= MSG_CMSG_CLOEXEC;
#  endif

    ssize_t bytesReceived = recvmsg(sockfd, &msg, flags);
    if (bytesReceived < 0)
        throw SysError("recvmsg");
    if (bytesReceived == 0)
        throw EndOfFile("connection closed");

    /* Sanity check: MSG_CTRUNC indicates control message was truncated.
       This should never happen since we size the buffer for the maximum
       number of FDs the kernel allows per message. */
    assert(!(msg.msg_flags & MSG_CTRUNC));

    // Extract file descriptors from control message
    std::vector<AutoCloseFD> fds;

    for (struct cmsghdr * cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            size_t count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            auto * fdPtr = reinterpret_cast<int *>(CMSG_DATA(cmsg));

            fds.reserve(count);
            for (size_t i = 0; i < count; ++i) {
#  ifndef MSG_CMSG_CLOEXEC
                /* If we couldn't receive them with this already set, we
                   manually set it ourselves. */
                closeOnExec(fdPtr[i]);
#  endif
                fds.emplace_back(fdPtr[i]);
            }
        }
    }

    return {static_cast<size_t>(bytesReceived), std::move(fds)};
}

#endif

} // namespace nix
