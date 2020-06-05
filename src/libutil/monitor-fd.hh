#pragma once

#include <thread>
#include <atomic>

#include <cstdlib>
#include <poll.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

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
              struct pollfd fds[1];
              fds[0].fd = fd;
              /* This shouldn't be necessary, but macOS doesn't seem to
                 like a zeroed out events field.
                 See rdar://37537852.
              */
              fds[0].events = POLLHUP;
              auto count = poll(fds, 1, -1);
              if (count == -1) abort(); // can't happen
              /* This shouldn't happen, but can on macOS due to a bug.
                 See rdar://37550628.

                 This may eventually need a delay or further
                 coordination with the main thread if spinning proves
                 too harmful.
               */
              if (count == 0) continue;
              assert(fds[0].revents & POLLHUP);
              triggerInterrupt();
              break;
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
