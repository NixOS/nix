#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/util/file-descriptor.hh"
#include "nix/util/file-system.hh"
#include "nix/util/finally.hh"
#include "nix/util/processes.hh"

#include <chrono>
#include <thread>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>

namespace nix {

TEST(AutoCloseFD, DestructorClosesFd)
{
    Pipe pipe;
    pipe.create();
    int rawFd = pipe.readSide.get();

    // Move the fd into an inner scope; on exit, the destructor must close it.
    {
        AutoCloseFD owner(pipe.readSide.release());
        ASSERT_EQ(owner.get(), rawFd);
    }

    // Confirm the kernel closed the fd: an explicit close() must now fail
    // with EBADF, proving the destructor did the close (otherwise we'd
    // succeed in closing a still-open fd).
    int rc = ::close(rawFd);
    EXPECT_EQ(rc, -1);
    EXPECT_EQ(errno, EBADF);
}

TEST(AutoCloseFD, MoveAssignClosesPreviousFd)
{
    Pipe pipe1;
    pipe1.create();
    Pipe pipe2;
    pipe2.create();

    int prevFd = pipe1.readSide.get();
    AutoCloseFD a(pipe1.readSide.release());
    AutoCloseFD b(pipe2.readSide.release());

    // Move-assign: a's previous fd must be closed, then b's fd moved into a.
    a = std::move(b);

    int rc = ::close(prevFd);
    EXPECT_EQ(rc, -1);
    EXPECT_EQ(errno, EBADF) << "operator= must close the previously-owned fd";

    EXPECT_FALSE(b);
}

TEST(AutoCloseFD, ReleaseSurrendersOwnership)
{
    Pipe pipe;
    pipe.create();
    int rawFd = pipe.readSide.get();

    Descriptor surrendered;
    {
        AutoCloseFD owner(pipe.readSide.release());
        surrendered = owner.release();
        EXPECT_EQ(surrendered, rawFd);
        EXPECT_FALSE(owner) << "after release, the AutoCloseFD must look empty";
    }
    // Destructor must NOT have closed: the released fd is still usable.
    int rc = ::close(surrendered);
    EXPECT_EQ(rc, 0);
}

TEST(AutoCloseFD, BoolConversionReflectsFdValidity)
{
    AutoCloseFD empty;
    EXPECT_FALSE(empty);

    Pipe pipe;
    pipe.create();
    AutoCloseFD owner(pipe.readSide.release());
    EXPECT_TRUE(owner);
}

TEST(Pipe, CloseClosesBothEnds)
{
    Pipe pipe;
    pipe.create();
    int r = pipe.readSide.get();
    int w = pipe.writeSide.get();

    pipe.close();

    // Both raw fds must now be unusable.
    EXPECT_EQ(::close(r), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(::close(w), -1);
    EXPECT_EQ(errno, EBADF);
}

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

TEST(PipeCreate, ThrowsWhenDescriptorLimitReached)
{
    struct rlimit orig;
    ASSERT_EQ(getrlimit(RLIMIT_NOFILE, &orig), 0);
    // Restore via Finally rather than a bare trailing setrlimit call, so a
    // failed assertion below still leaves the process at its original fd
    // limit instead of a lowered one for the rest of the test binary.
    Finally restoreLimit([&] { EXPECT_EQ(setrlimit(RLIMIT_NOFILE, &orig), 0); });

    /* Lower the soft limit to just above the highest fd currently in use, so
       the pipe2() call inside Pipe::create() below has no room left and
       fails with EMFILE. */
    int highest = -1;
#ifdef __linux__
    for (auto & entry : DirectoryIterator{"/proc/self/fd"}) {
        highest = std::max(highest, std::stoi(entry.path().filename()));
    }
#else
    // No /proc on macOS/BSD: probe each candidate fd directly instead.
    int maxFD = sysconf(_SC_OPEN_MAX);
    for (int fd = 0; fd < maxFD; ++fd)
        if (fcntl(fd, F_GETFD) != -1)
            highest = fd;
#endif
    struct rlimit lowered = orig;
    lowered.rlim_cur = highest + 1;
    ASSERT_EQ(setrlimit(RLIMIT_NOFILE, &lowered), 0);

    Pipe pipe;
    EXPECT_THROW(pipe.create(), SysError);
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

TEST(DupDescriptor, SetsCloseOnExecFlag)
{
    Pipe pipe;
    pipe.create();

    auto dup = dupDescriptor(pipe.writeSide.get());
    int flags = fcntl(dup.get(), F_GETFD);
    ASSERT_NE(flags, -1);
    EXPECT_TRUE(flags & FD_CLOEXEC);
}

TEST(CloseOnExec, SetsFlag)
{
    Pipe pipe;
    pipe.create();

    // pipe.create() already sets close-on-exec via pipe2/O_CLOEXEC; clear it first so this
    // test actually exercises unix::closeOnExec rather than observing pipe2's own flag.
    ASSERT_NE(fcntl(pipe.writeSide.get(), F_SETFD, 0), -1);
    ASSERT_EQ(fcntl(pipe.writeSide.get(), F_GETFD), 0);

    unix::closeOnExec(pipe.writeSide.get());

    int flags = fcntl(pipe.writeSide.get(), F_GETFD);
    ASSERT_NE(flags, -1);
    EXPECT_TRUE(flags & FD_CLOEXEC);
}

TEST(CloseOnExec, ThrowsOnInvalidDescriptor)
{
    EXPECT_THROW(unix::closeOnExec(INVALID_DESCRIPTOR), SysError);
}

TEST(SelfPipe, NotifyAndDrainRoundTrip)
{
    unix::SelfPipe sp;
    sp.create();
    sp.notify();
    EXPECT_NO_THROW(sp.drain());
}

TEST(SelfPipe, DrainOnEmptyPipeReturnsPromptly)
{
    unix::SelfPipe sp;
    sp.create();
    // No notify() was called; the pipe is empty. Because create() makes it non-blocking,
    // drain() must return immediately (on EAGAIN) rather than block waiting for data.
    EXPECT_NO_THROW(sp.drain());
}

TEST(SelfPipe, NotifyWhenFullDoesNotThrow)
{
    unix::SelfPipe sp;
    sp.create();
    // Notify comfortably past any default pipe capacity, driving the pipe to EAGAIN (full).
    // notify() must swallow that, not throw.
    for (int i = 0; i < 200000; i++) {
        EXPECT_NO_THROW(sp.notify());
    }
    sp.drain();
}

TEST(SelfPipe, DrainEmptiesAllPendingNotifies)
{
    unix::SelfPipe sp;
    sp.create();
    // More bytes than drain()'s 128-byte read buffer, forcing multiple read() calls inside
    // a single drain().
    for (int i = 0; i < 300; i++)
        sp.notify();
    sp.drain();

    // The pipe must now be fully drained: a direct non-blocking read returns EAGAIN.
    char c;
    ssize_t n = ::read(sp.pipe.readSide.get(), &c, 1);
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, EAGAIN);
}

TEST(SelfPipe, NotifyThrowsOnWriteFailureOtherThanEagain)
{
    unix::SelfPipe sp;
    sp.create();
    sp.pipe.writeSide.close();
    EXPECT_THROW(sp.notify(), SysError);
}

TEST(SelfPipe, DrainThrowsOnReadFailureOtherThanEagain)
{
    unix::SelfPipe sp;
    sp.create();
    sp.pipe.readSide.close();
    EXPECT_THROW(sp.drain(), SysError);
}

} // namespace nix
