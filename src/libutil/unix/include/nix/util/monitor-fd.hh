#pragma once
///@file

#include <thread>
#include <cassert>

#include <poll.h>
#include <errno.h>

#ifdef __APPLE__
#  include <sys/types.h>
#  include <sys/event.h>
#endif

#include "nix/util/signals.hh"
#include "nix/util/file-descriptor.hh"

namespace nix {

class MonitorFdHup
{
private:
    std::thread thread;
    Pipe notifyPipe;

    void runThread(int watchFd, int notifyFd);

public:
    MonitorFdHup(int fd);

    ~MonitorFdHup()
    {
        // Close the write side to signal termination via POLLHUP
        notifyPipe.writeSide.close();
        thread.join();
    }
};

#ifdef __APPLE__
/* This custom kqueue usage exists because Apple's poll implementation is
 * broken and loses event subscriptions if EVFILT_READ fires without matching
 * the requested `events` in the pollfd.
 *
 * We use EVFILT_READ, which causes some spurious wakeups (at most one per write
 * from the client, in addition to the socket lifecycle events), because the
 * alternate API, EVFILT_SOCK, doesn't work on pipes, which this is also used
 * to monitor in certain situations.
 *
 * See (EVFILT_SOCK):
 * https://github.com/netty/netty/blob/64bd2f4eb62c2fb906bc443a2aabf894c8b7dce9/transport-classes-kqueue/src/main/java/io/netty/channel/kqueue/AbstractKQueueChannel.java#L434
 *
 * See: https://git.lix.systems/lix-project/lix/issues/729
 * Apple bug in poll(2): FB17447257, available at https://openradar.appspot.com/FB17447257
 */
inline void MonitorFdHup::runThread(int watchFd, int notifyFd)
{
    int kqResult = kqueue();
    if (kqResult < 0) {
        throw SysError("MonitorFdHup kqueue");
    }
    AutoCloseFD kq{kqResult};

    std::array<struct kevent, 2> kevs;

    // kj uses EVFILT_WRITE for this, but it seems that it causes more spurious
    // wakeups in our case of doing blocking IO from another thread compared to
    // EVFILT_READ.
    //
    // EVFILT_WRITE and EVFILT_READ (for sockets at least, where I am familiar
    // with the internals) both go through a common filter which catches EOFs
    // and generates spurious wakeups for either readable/writable events.
    EV_SET(&kevs[0], watchFd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, nullptr);
    EV_SET(&kevs[1], notifyFd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, nullptr);

    int result = kevent(kq.get(), kevs.data(), kevs.size(), nullptr, 0, nullptr);
    if (result < 0) {
        throw SysError("MonitorFdHup kevent add");
    }

    while (true) {
        struct kevent event;
        int numEvents = kevent(kq.get(), nullptr, 0, &event, 1, nullptr);
        if (numEvents < 0) {
            throw SysError("MonitorFdHup kevent watch");
        }

        if (numEvents > 0 && (event.flags & EV_EOF)) {
            if (event.ident == uintptr_t(watchFd)) {
                unix::triggerInterrupt();
            }
            // Either watched fd or notify fd closed, exit
            return;
        }
    }
}
#else
inline void MonitorFdHup::runThread(int watchFd, int notifyFd)
{
    while (true) {
        struct pollfd fds[2];
        fds[0].fd = watchFd;
        fds[0].events = 0; // POSIX: POLLHUP is always reported
        fds[1].fd = notifyFd;
        fds[1].events = 0;

        auto count = poll(fds, 2, -1);
        if (count == -1) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            } else {
                throw SysError("in MonitorFdHup poll()");
            }
        }

        if (fds[0].revents & POLLHUP) {
            unix::triggerInterrupt();
            break;
        }

        if (fds[1].revents & POLLHUP) {
            // Notify pipe closed, exit thread
            break;
        }
    }
}
#endif

inline MonitorFdHup::MonitorFdHup(int fd)
{
    notifyPipe.create();
    int notifyFd = notifyPipe.readSide.get();
    thread = std::thread([this, fd, notifyFd]() { this->runThread(fd, notifyFd); });
};

} // namespace nix
