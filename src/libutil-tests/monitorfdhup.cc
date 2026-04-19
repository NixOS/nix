// TODO: investigate why this is hanging on cygwin
#if !defined(_WIN32) && !defined(__CYGWIN__)

#  include "nix/util/monitor-fd.hh"
#  include "nix/util/signals.hh"

#  include <sys/file.h>
#  include <sys/socket.h>
#  include <chrono>
#  include <gtest/gtest.h>

namespace nix {

// MonitorFdHup calls triggerInterrupt() when it detects a hangup,
// which sets a process-global flag.  We must clear it after each
// test so subsequent tests that call checkInterrupt() are not
// poisoned.
class MonitorFdHupTest : public ::testing::Test {
protected:
    void TearDown() override {
        setInterrupted(false);
    }
};

TEST_F(MonitorFdHupTest, shouldNotBlock)
{
    Pipe p;
    p.create();
    {
        // when monitor gets destroyed it should cancel the
        // background thread and do not block
        MonitorFdHup monitor(p.readSide.get());
    }
}

TEST_F(MonitorFdHupTest, shouldExitOnPeerClose)
{
    // When the peer end of a socket is closed, MonitorFdHup should
    // detect the hangup and exit its poll loop promptly.
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    AutoCloseFD our(fds[0]);
    AutoCloseFD peer(fds[1]);

    auto start = std::chrono::steady_clock::now();
    {
        MonitorFdHup monitor(our.get());
        // Close the peer end — delivers POLLHUP to our end.
        peer.close();
        // Small delay to let the monitor thread see it.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // Destructor joins the thread — should return immediately
        // since the thread already exited on POLLHUP.
    }
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    // Should complete well under 1 second.  If the thread is spinning
    // it would still complete (destructor closes notifyPipe) but after
    // a measurable delay from the spin consuming CPU.
    EXPECT_LT(ms, 1000);
}

#  ifndef __APPLE__
// On Linux, poll() returns POLLNVAL for a closed/invalid fd.
// MonitorFdHup must handle this without spinning.
// On macOS, kqueue is used instead of poll, and registering a
// closed fd with kevent fails differently.
TEST_F(MonitorFdHupTest, shouldExitOnInvalidFd)
{
    // Close the fd before creating MonitorFdHup.
    // The poll loop should see POLLNVAL on the first poll() call
    // and exit immediately via the POLLHUP|POLLERR|POLLNVAL check.
    Pipe p;
    p.create();
    int fd = p.readSide.get();
    p.readSide.close();

    auto start = std::chrono::steady_clock::now();
    {
        MonitorFdHup monitor(fd);
        // Give the thread a moment to run and (hopefully) exit.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        // Destructor joins.  If the thread is spinning on POLLNVAL
        // without our fix, it would still be alive and only exit
        // when the destructor closes notifyPipe.  The spin would
        // be visible as high CPU, though the join would succeed.
    }
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    // Should complete promptly.  Without the POLLNVAL fix, the thread
    // spins at ~2M iterations/sec until the destructor breaks it out,
    // but it still completes — the test is mainly documenting that
    // this code path is exercised.
    EXPECT_LT(ms, 1000);
}
#  endif

TEST_F(MonitorFdHupTest, shouldExitOnShutdown)
{
    // When the peer calls shutdown(SHUT_RDWR), MonitorFdHup should
    // detect the condition and exit.
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    AutoCloseFD our(fds[0]);
    AutoCloseFD peer(fds[1]);

    auto start = std::chrono::steady_clock::now();
    {
        MonitorFdHup monitor(our.get());
        // Shut down the peer — on most platforms this delivers POLLHUP.
        shutdown(peer.get(), SHUT_RDWR);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    EXPECT_LT(ms, 1000);
}

} // namespace nix

#endif
