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

public:
    MonitorFdHup(int fd)
    {
        thread = std::thread([fd]() {
            while (true) {
                /* Wait indefinitely until a POLLHUP occurs. */
                constexpr size_t num_fds = 1;
                struct pollfd fds[num_fds] = {
                    {
                        .fd = fd,
                        .events =
                /* Polling for no specific events (i.e. just waiting
                   for an error/hangup) doesn't work on macOS
                   anymore. So wait for read events and ignore
                   them. */
#ifdef __APPLE__
                            POLLRDNORM,
#else
                            0,
#endif
                    },
                };

                auto count = poll(fds, num_fds, -1);
                if (count == -1) {
                    if (errno == EINTR || errno == EAGAIN)
                        continue;
                    throw SysError("failed to poll() in MonitorFdHup");
                }

                /* This shouldn't happen, but can on macOS due to a bug.
                   See rdar://37550628.

                   This may eventually need a delay or further
                   coordination with the main thread if spinning proves
                   too harmful.
                */
                if (count == 0)
                    continue;
                if (fds[0].revents & POLLHUP) {
                    unix::triggerInterrupt();
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
        pthread_cancel(thread.native_handle());
        thread.join();
    }
};

}
