#include <gtest/gtest.h>

#include "nix/util/file-descriptor.hh"
#include "nix/util/processes.hh"
#include "nix/util/unix-domain-socket.hh"

#include <algorithm>
#include <sys/socket.h>

namespace nix {

using namespace nix::unix;

/* ----------------------------------------------------------------------------
 * sendMessageWithFds / receiveMessageWithFds
 * --------------------------------------------------------------------------*/

TEST(MessageWithFds, streamWithData)
{
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

} // namespace nix
