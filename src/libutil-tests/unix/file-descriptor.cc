#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/util/file-descriptor.hh"
#include "nix/util/processes.hh"

#include <unistd.h>
#include <array>

namespace nix {

TEST(closeExtraFDs, works)
{
    using namespace nix::unix;

    Pipe pipe;
    pipe.create();
    Pid pid = startProcess([&]() {
        closeExtraFDs();

        /* File descriptors should already be closed by `closeExtraFDs`. */
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

TEST(closeExtraFDs, keepExtra)
{
    using namespace nix::unix;

    Pipe pipe1, pipe2;
    pipe1.create();
    pipe2.create();
    Pid pid = startProcess([&]() {
        closeExtraFDs(std::to_array({pipe1.readSide.get(), pipe2.readSide.get()}));

        /* File descriptors should already be closed by `closeExtraFDs`. */
        for (int fd : {pipe1.writeSide.get(), pipe2.writeSide.get()}) {
            if (::close(fd) == 0 || errno != EBADF)
                _exit(1);
        }

        /* readSide, stdin, stdout and stderr should not be closed. */
        for (int fd : {pipe1.readSide.get(), pipe2.readSide.get(), STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
            if (::close(fd) == -1)
                _exit(2);
        }

        _exit(0);
    });

    ASSERT_NE(pid_t(pid), -1);
    ASSERT_TRUE(statusOk(pid.wait()));
}

} // namespace nix
