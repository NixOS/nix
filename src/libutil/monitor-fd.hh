#pragma once

#include <thread>
#include <atomic>

#include <poll.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

namespace nix {


class MonitorFdHup
{
private:
    std::thread thread;
    std::atomic_bool quit;

public:
    MonitorFdHup(int fd)
    {
        quit = false;
        thread = std::thread([&]() {
            /* Wait indefinitely until a POLLHUP occurs. */
            struct pollfd fds[1];
            fds[0].fd = fd;
            fds[0].events = 0;
            if (poll(fds, 1, -1) == -1) {
                if (errno != EINTR) abort(); // can't happen
                assert(quit);
                return; // destructor is asking us to exit
            }
            assert(fds[0].revents & POLLHUP);
            /* We got POLLHUP, so send an INT signal to the main thread. */
            kill(getpid(), SIGINT);
        });
    };

    ~MonitorFdHup()
    {
        quit = true;
        pthread_kill(thread.native_handle(), SIGINT);
        thread.join();
    }
};


}
