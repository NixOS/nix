#include <gtest/gtest.h>

#include "nix/util/file-descriptor.hh"
#include "nix/util/processes.hh"
#include "nix/util/unix-domain-socket.hh"

#include <sys/socket.h>

namespace nix {

using namespace nix::unix;

/* ----------------------------------------------------------------------------
 * sendMessageWithFds / receiveMessageWithFds
 * --------------------------------------------------------------------------*/

TEST(MessageWithFds, sendAndReceiveWithFds)
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

    std::string_view testData = "test with fds";
    std::string_view pipeMsg = "hello from parent";

    Pid pid = startProcess([&] {
        sender.close(); // Child only needs receiver

        std::array<std::byte, 64> buffer;
        auto result = receiveMessageWithFds(receiver.get(), buffer);

        if (result.bytesReceived != testData.size())
            _exit(1);
        if (std::string_view(reinterpret_cast<char *>(buffer.data()), result.bytesReceived) != testData)
            _exit(2);
        if (result.fds.size() != numPipes)
            _exit(3);

        // Read from the first received fd to verify it works
        std::array<std::byte, 64> readBuf;
        size_t n = read(result.fds[0].get(), readBuf);
        if (n != pipeMsg.size())
            _exit(4);
        if (std::string_view(reinterpret_cast<char *>(readBuf.data()), n) != pipeMsg)
            _exit(5);

        _exit(0);
    });

    receiver.close(); // Parent only needs sender

    // Send message with all the pipe read fds
    sendMessageWithFds(sender.get(), testData, fdsToSend);
    for (auto & p : pipes)
        p.readSide.close(); // Close our copies after sending

    // Write to the first pipe so child can read from the received fd
    ASSERT_EQ(write(pipes[0].writeSide.get(), pipeMsg.data(), pipeMsg.size()), static_cast<ssize_t>(pipeMsg.size()));
    for (auto & p : pipes)
        p.writeSide.close();

    int status = pid.wait();
    ASSERT_TRUE(statusOk(status));
}

} // namespace nix
