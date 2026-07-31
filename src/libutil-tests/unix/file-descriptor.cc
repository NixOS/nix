#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/util/file-descriptor.hh"
#include "nix/util/processes.hh"

#include <unistd.h>

namespace nix {

TEST(closeExtraFDs, works)
{
    Pipe pipe;
    pipe.create();
    Pid pid = startProcess([&]() {
        unix::closeExtraFDs();

        /* File descriptors should already be closed by unix::closeExtraFDs(). */
        for (int fd : {pipe.readSide.get(), pipe.writeSide.get()}) {
            if (::close(fd) == 0 || errno != EBADF)
                _exit(1);
        }

        /* stdin, stdout and stderr should not be closed. */
        for (int fd : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
            if (::close(fd) == -1)
                _exit(2);
        }

        _exit(0);
    });

    ASSERT_NE(pid_t(pid), -1);
    ASSERT_TRUE(statusOk(pid.wait()));
}

} // namespace nix
