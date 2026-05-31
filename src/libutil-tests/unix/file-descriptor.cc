#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/util/file-descriptor.hh"
#include "nix/util/processes.hh"

#include <chrono>
#include <thread>
#include <unistd.h>

namespace nix {

TEST(WriteFull, LoopsUntilAllBytesWritten)
{
    // Use a non-blocking pipe so write() returns short once the kernel
    // buffer is full. writeFull's retryOnBlock then polls for room and
    // resumes; the reader drains in small chunks to keep room scarce.
    // Verifies the loop's both `count` and `buf` (via remove_prefix)
    // advance: a single-shot variant would write only ~PIPE_BUF and stop.
    Pipe pipe;
    pipe.create(/*nonBlocking=*/true);

    constexpr size_t total = 256 * 1024; // 256 KiB exceeds typical pipe buf
    std::string payload(total, '\0');
    for (size_t i = 0; i < total; ++i)
        payload[i] = static_cast<char>(i & 0xff);

    std::string drained;
    std::thread reader([&]() {
        // Use raw POSIX read so we can tolerate EAGAIN with a small sleep,
        // mirroring what retryOnBlock does on the writer side. Stops on EOF.
        char buf[2048];
        while (drained.size() < total) {
            ssize_t n = ::read(pipe.readSide.get(), buf, sizeof(buf));
            if (n > 0) {
                drained.append(buf, n);
            } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                break;
            }
        }
    });

    writeFull(pipe.writeSide.get(), payload, /*allowInterrupts=*/false);
    reader.join();
    pipe.writeSide.close();

    ASSERT_EQ(drained.size(), total);
    EXPECT_EQ(drained, payload);
}

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
