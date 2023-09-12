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
#include "file-descriptor.hh"

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
                /* Polling for no specific events (i.e. just waiting
                   for an error/hangup) doesn't work on macOS
                   anymore. So wait for read events and ignore
                   them. */
                // FIXME(jade): we have looked at the XNU kernel code and as
                // far as we can tell, the above is bogus. It should be the
                // case that the previous version of this and the current
                // version are identical: waiting for POLLHUP and POLLRDNORM in
                // the kernel *should* be identical.
                // https://github.com/apple-oss-distributions/xnu/blob/94d3b452840153a99b38a3a9659680b2a006908e/bsd/kern/sys_generic.c#L1751-L1758
                //
                // So, this needs actual testing and we need to figure out if
                // this is actually bogus.
                short hangup_events =
#ifdef __APPLE__
                    POLLRDNORM
#else
                    0
#endif
                    ;

                /* Wait indefinitely until a POLLHUP occurs. */
                constexpr size_t num_fds = 2;
                struct pollfd fds[num_fds] = {
                    {
                        .fd = fd,
                        .events = hangup_events,
                    },
                    {
                        .fd = notifyPipe.readSide.get(),
                        .events = hangup_events,
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
