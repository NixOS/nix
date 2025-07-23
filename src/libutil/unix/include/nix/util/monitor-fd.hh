#pragma once
///@file

#include <thread>
#include <atomic>

#include <cstdlib>
#include <poll.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "nix/util/signals.hh"

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
                // There is a POSIX violation on macOS: you have to listen for
                // at least POLLHUP to receive HUP events for a FD. POSIX says
                // this is not so, and you should just receive them regardless.
                // However, as of our testing on macOS 14.5, the events do not
                // get delivered if in the all-bits-unset case, but do get
                // delivered if `POLLHUP` is set.
                //
                // This bug filed as rdar://37537852
                // (https://openradar.appspot.com/37537852).
                //
                // macOS's own man page
                // (https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/poll.2.html)
                // additionally says that `POLLHUP` is ignored as an input. It
                // seems the likely order of events here was
                //
                // 1. macOS did not follow the POSIX spec
                //
                // 2. Somebody ninja-fixed this other spec violation to make
                // sure `POLLHUP` was not forgotten about, even though they
                // "fixed" this issue in a spec-non-compliant way. Whatever,
                // we'll use the fix.
                //
                // Relevant code, current version, which shows the :
                // https://github.com/apple-oss-distributions/xnu/blob/94d3b452840153a99b38a3a9659680b2a006908e/bsd/kern/sys_generic.c#L1751-L1758
                //
                // The `POLLHUP` detection was added in
                // https://github.com/apple-oss-distributions/xnu/commit/e13b1fa57645afc8a7b2e7d868fe9845c6b08c40#diff-a5aa0b0e7f4d866ca417f60702689fc797e9cdfe33b601b05ccf43086c35d395R1468
                // That means added in 2007 or earlier. Should be good enough
                // for us.
                short hangup_events =
#ifdef __APPLE__
                    POLLHUP
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
                // On macOS, (jade thinks that) it is possible (although not
                // observed on macOS 14.5) that in some limited cases on buggy
                // kernel versions, all the non-POLLHUP events for the socket
                // get delivered.
                //
                // We could sleep to avoid pointlessly spinning a thread on
                // those, but this opens up a different problem, which is that
                // if do sleep, it will be longer before the daemon fork for a
                // client exits. Imagine a sequential shell script, running Nix
                // commands, each of which talk to the daemon. If the previous
                // command registered a temp root, exits, and then the next
                // command issues a delete request before the temp root is
                // cleaned up, that delete request might fail.
                //
                // Not sleeping doesn't actually fix the race condition --- we
                // would need to block on the old connections' tempt roots being
                // cleaned up in in the new connection --- but it does make it
                // much less likely.
            }
        });
    };

    ~MonitorFdHup()
    {
        notifyPipe.writeSide.close();
        thread.join();
    }
};

} // namespace nix
