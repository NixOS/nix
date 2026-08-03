#include <gtest/gtest.h>

#include "nix/util/file-descriptor.hh"
#include "nix/util/file-system.hh"
#include "nix/util/processes.hh"
#include "nix/util/unix-domain-socket.hh"

#include <algorithm>
#include <filesystem>
#include <poll.h>
#include <span>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace nix {

/* ----------------------------------------------------------------------------
 * sendMessageWithFds / receiveMessageWithFds
 * --------------------------------------------------------------------------*/

TEST(MessageWithFds, streamWithData)
{
    using namespace nix::unix;

    int sockets[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    AutoCloseFD sender(sockets[0]);
    AutoCloseFD receiver(sockets[1]);

    // Create multiple pipes to test sending many fds at once
    constexpr size_t numPipes = 8;
    std::vector<Pipe> pipes(numPipes);
    std::vector<int> fdsToSend;
    for (auto & p : pipes) {
        p.create();
        fdsToSend.push_back(p.readSide.get());
    }

    auto testData = std::as_bytes(std::span{"test with fds"});
    auto pipeMsg = std::as_bytes(std::span{std::string_view{"hello from parent"}});

    Pid pid = startProcess([&] {
        sender.close(); // Child only needs receiver
        for (auto & p : pipes) {
            p.readSide.close();
            p.writeSide.close();
        }

        std::array<std::byte, 64> buffer;
        auto result = receiveMessageWithFds(receiver.get(), buffer);

        if (!std::ranges::equal(std::span{buffer.data(), result.bytesReceived}, testData))
            _exit(1);
        if (result.fds.size() != numPipes)
            _exit(2);

        // Read from the first received fd to verify it works
        std::array<std::byte, 64> readBuf;
        size_t n = read(result.fds[0].get(), readBuf);
        if (!std::ranges::equal(std::span{readBuf.data(), n}, pipeMsg))
            _exit(3);

        _exit(0);
    });

    receiver.close(); // Parent only needs sender

    // Send message with all the pipe read fds
    sendMessageWithFds(sender.get(), testData, fdsToSend);
    for (auto & p : pipes)
        p.readSide.close(); // Close our copies after sending

    // Write to the first pipe so child can read from the received fd
    ASSERT_EQ(write(pipes[0].writeSide.get(), pipeMsg, false), pipeMsg.size());
    for (auto & p : pipes)
        p.writeSide.close();

    int status = pid.wait();
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(MessageWithFds, datagramEmptyData)
{
    using namespace nix::unix;

    int sockets[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets), 0);
    AutoCloseFD sender(sockets[0]);
    AutoCloseFD receiver(sockets[1]);

    Pipe pipe;
    pipe.create();
    std::vector<int> fdsToSend{pipe.readSide.get()};

    auto pipeMsg = std::as_bytes(std::span{std::string_view{"hello from parent"}});

    Pid pid = startProcess([&] {
        sender.close();
        pipe.readSide.close();
        pipe.writeSide.close();

        std::array<std::byte, 64> buffer;
        auto result = receiveMessageWithFds(receiver.get(), buffer);

        if (result.fds.size() != 1)
            _exit(1);

        // Verify the received fd works
        std::array<std::byte, 64> readBuf;
        size_t n = read(result.fds[0].get(), readBuf);
        if (!std::ranges::equal(std::span{readBuf.data(), n}, pipeMsg))
            _exit(2);

        _exit(0);
    });

    receiver.close();

    sendMessageWithFds(sender.get(), {}, fdsToSend);
    pipe.readSide.close();

    ASSERT_EQ(write(pipe.writeSide.get(), pipeMsg, false), pipeMsg.size());
    pipe.writeSide.close();

    int status = pid.wait();
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

namespace {

std::filesystem::path buildDeepDir(const std::filesystem::path & root, size_t targetLen)
{
    std::filesystem::path cur = root;
    while (cur.string().size() < targetLen) {
        cur /= "deeper-segment-padding";
        std::filesystem::create_directory(cur);
    }
    return cur;
}

// This listens at sockPath and has a forked child connect and write one byte. If
// the child can't connect, the readyPipe wakes the poll so accept doesn't hang.
void runRoundtrip(const std::filesystem::path & sockPath)
{
    auto listenFd = createUnixDomainSocket(sockPath, 0600);

    Pipe readyPipe;
    readyPipe.create();

    Pid pid = startProcess([&] {
        readyPipe.readSide.close();
        int rc = 1;
        try {
            auto clientFd = connect(sockPath);
            std::byte msg{'x'};
            if (write(clientFd.get(), std::span{&msg, 1}, false) != 1)
                rc = 2;
            else
                rc = 0;
        } catch (...) {
            rc = 3;
        }
        std::byte done{static_cast<std::byte>(rc)};
        (void) write(readyPipe.writeSide.get(), std::span{&done, 1}, false);
        _exit(rc);
    });
    readyPipe.writeSide.close();

    // Multiplex accept and the readyPipe so a child failure doesn't hang.
    pollfd pfds[2] = {
        {.fd = toSocket(listenFd.get()), .events = POLLIN, .revents = 0},
        {.fd = readyPipe.readSide.get(), .events = POLLIN, .revents = 0},
    };
    ASSERT_GT(poll(pfds, 2, 5000), 0) << "timed out waiting for client";
    ASSERT_TRUE(pfds[0].revents & POLLIN) << "child signalled failure before connecting";

    int conn = accept(toSocket(listenFd.get()), nullptr, nullptr);
    ASSERT_NE(conn, -1);
    AutoCloseFD connFd(conn);

    std::byte buf{};
    ASSERT_EQ(read(connFd.get(), std::span{&buf, 1}), size_t{1});
    EXPECT_EQ(std::to_integer<char>(buf), 'x');

    int status = pid.wait();
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

} // namespace

class UnixDomainSocketLongPath : public ::testing::Test
{
protected:
    void SetUp() override
    {
        tmp = createTempDir();
    }

    void TearDown() override
    {
        nix::deletePath(tmp);
    }

    std::filesystem::path tmp;
};

TEST_F(UnixDomainSocketLongPath, bindConnectRoundtrip)
{
    // The full path exceeds sun_path.
    constexpr size_t targetLen = sizeof(sockaddr_un::sun_path) + 50;
    auto deepDir = buildDeepDir(tmp, targetLen);
    auto sockPath = deepDir / "s";
    ASSERT_GT(sockPath.string().size(), sizeof(sockaddr_un::sun_path));

    runRoundtrip(sockPath);
}

TEST_F(UnixDomainSocketLongPath, bindRejectsTooLongBasename)
{
    // A basename over sun_path cannot be rescued by chdir or dirfd tricks.
    std::string base(sizeof(sockaddr_un::sun_path), 'a');
    auto sockPath = tmp / base;

    auto fd = createUnixDomainSocket();
    EXPECT_THROW(bind(toSocket(fd.get()), sockPath), Error);
}

TEST_F(UnixDomainSocketLongPath, nearLimitBasenameRoundtrip)
{
    // A basename close to the sun_path limit. The roundtrip works on both platforms,
    // but it forces Linux through the fork+chdir fallback while FreeBSD stays on bindat.
    auto deepDir = buildDeepDir(tmp, sizeof(sockaddr_un::sun_path));
    std::string base(sizeof(sockaddr_un::sun_path) - 4, 'b');
    auto sockPath = deepDir / base;

    ASSERT_GT(sockPath.string().size(), sizeof(sockaddr_un::sun_path));
    ASSERT_LT(base.size() + 1, sizeof(sockaddr_un::sun_path));

    runRoundtrip(sockPath);
}

} // namespace nix
