#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/util/file-descriptor.hh"
#include "nix/util/processes.hh"

#include <chrono>
#include <thread>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

namespace nix {

TEST(ReadFile, ReadsFullContentsOfRegularFile)
{
    // Build the file via a pipe so we don't depend on the filesystem; use
    // a memfd-equivalent so getFileSize returns the actual byte count.
    char tmpl[] = "/tmp/nix-readfile-XXXXXX";
    int fd = mkstemp(tmpl);
    ASSERT_NE(fd, -1);
    unlink(tmpl);

    std::string payload(8192, 'x');
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<char>((i * 7 + 3) & 0xff);
    ASSERT_EQ(::write(fd, payload.data(), payload.size()), (ssize_t) payload.size());
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);

    auto got = readFile(fd);
    EXPECT_EQ(got, payload);
    close(fd);
}

#ifdef __linux__
TEST(ReadFile, ToleratesNominalSizeZeroFromProc)
{
    // /proc files like /proc/version report size 0 from fstat, yet hold real
    // content. readFile must drain to EOF without trusting getFileSize.
    int fd = open("/proc/version", O_RDONLY);
    if (fd == -1) {
        GTEST_SKIP() << "/proc/version not available";
    }
    if (getFileSize(fd) != 0) {
        close(fd);
        GTEST_SKIP() << "/proc/version did not report a nominal size of 0 on this system";
    }
    auto got = readFile(fd);
    EXPECT_FALSE(got.empty());
    EXPECT_NE(got.find("Linux"), std::string::npos);
    close(fd);
}
#endif

TEST(DrainFD, NonBlockingStopsAtEAGAIN)
{
    // With block=false, drainFD must set O_NONBLOCK, return the bytes
    // currently available, then restore the original flags.
    Pipe pipe;
    pipe.create(); // default: blocking

    writeFull(pipe.writeSide.get(), "ready", /*allowInterrupts=*/false);
    // Leave the write side OPEN: blocking-mode drainFD would deadlock
    // here, so passing the test proves block=false took effect.

    auto got = drainFD(pipe.readSide.get(), {.block = false});
    EXPECT_EQ(got, "ready");

    // Original flags must be restored: a subsequent blocking read still
    // works synchronously when more data arrives.
    writeFull(pipe.writeSide.get(), "more", /*allowInterrupts=*/false);
    pipe.writeSide.close();
    auto got2 = drainFD(pipe.readSide.get());
    EXPECT_EQ(got2, "more");

    // Sanity-check by inspecting the flags directly: O_NONBLOCK must be
    // cleared after both drainFD calls return.
    int flags = fcntl(pipe.readSide.get(), F_GETFL);
    ASSERT_NE(flags, -1);
    EXPECT_EQ(flags & O_NONBLOCK, 0);
}

TEST(DrainFD, NonBlockingRestoresOriginalFlags)
{
    // Set a custom flag (O_APPEND on the read side of a pipe is benign but
    // visible via F_GETFL) and verify drainFD's Finally restores it.
    Pipe pipe;
    pipe.create();

    int initial = fcntl(pipe.readSide.get(), F_GETFL);
    ASSERT_NE(initial, -1);
    ASSERT_NE(fcntl(pipe.readSide.get(), F_SETFL, initial | O_APPEND), -1);
    int after = fcntl(pipe.readSide.get(), F_GETFL);
    ASSERT_EQ(after & O_APPEND, O_APPEND);

    writeFull(pipe.writeSide.get(), "x", /*allowInterrupts=*/false);
    auto got = drainFD(pipe.readSide.get(), {.block = false});
    EXPECT_EQ(got, "x");

    // O_APPEND must still be set after drainFD returns (Finally restored
    // the saved flags including O_APPEND, not just zeroed them).
    int restored = fcntl(pipe.readSide.get(), F_GETFL);
    ASSERT_NE(restored, -1);
    EXPECT_EQ(restored & O_APPEND, O_APPEND);
    EXPECT_EQ(restored & O_NONBLOCK, 0);

    pipe.writeSide.close();
}

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
