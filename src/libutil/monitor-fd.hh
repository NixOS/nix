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
            /* Wait indefinitely until a POLLHUP occurs. */
            struct pollfd fds[1];
            fds[0].fd = fd;
            fds[0].events = 0;
            if (poll(fds, 1, -1) == -1) abort(); // can't happen
            assert(fds[0].revents & POLLHUP);
            /* We got POLLHUP, so send an INT signal to the main thread. */
            kill(getpid(), SIGINT);
        });
    };

    ~MonitorFdHup()
    {
        pthread_cancel(thread.native_handle());
        thread.join();
    }
};


}
