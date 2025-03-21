#pragma once
///@file

#include <thread>
#include <atomic>

#include <cstdlib>
#include <poll.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "signals.hh"

namespace nix {


class MonitorFdHup
{
private:
    std::thread thread;
    Pipe notifyPipe;

public:
    MonitorFdHup(int fd)
    {
        notifyPipe.create();
        thread = std::thread([this, fd]() {
            while (true) {
                /* Wait indefinitely until a POLLHUP occurs. */
                struct pollfd fds[2];

                fds[0].fd = fd;
                /* Polling for no specific events (i.e. just waiting
                   for an error/hangup) doesn't work on macOS
                   anymore. So wait for read events and ignore
                   them. */
                fds[0].events =
                    #ifdef __APPLE__
                    POLLRDNORM
                    #else
                    0
                    #endif
                    ;
                fds[1].fd = notifyPipe.readSide.get();
                fds[1].events =
                    #ifdef __APPLE__
                    POLLRDNORM
                    #else
                    0
                    #endif
                    ;

                auto count = poll(fds, 2, -1);
                if (count == -1) {
                    if (errno == EINTR || errno == EAGAIN) continue;
                    throw SysError("failed to poll() in MonitorFdHup");
                }
                /* This shouldn't happen, but can on macOS due to a bug.
                   See rdar://37550628.

                   This may eventually need a delay or further
                   coordination with the main thread if spinning proves
                   too harmful.
                */
                if (count == 0) continue;
                if (fds[0].revents & POLLHUP) {
                    unix::triggerInterrupt();
                    break;
                }
                if (fds[1].revents & POLLHUP) {
                    break;
                }
                /* This will only happen on macOS. We sleep a bit to
                   avoid waking up too often if the client is sending
                   input. */
                sleep(1);
            }
        });
    };

    ~MonitorFdHup()
    {
        close(notifyPipe.writeSide.get());
        thread.join();
    }
};


}
