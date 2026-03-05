#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/socket.hh"

#include <filesystem>
#include <span>
#include <unistd.h>
#include <vector>

namespace nix {

/**
 * Create a Unix domain socket.
 */
AutoCloseFD createUnixDomainSocket();

/**
 * Create a Unix domain socket in listen mode.
 */
AutoCloseFD createUnixDomainSocket(const std::filesystem::path & path, mode_t mode);

/**
 * Bind a Unix domain socket to a path.
 */
void bind(Socket fd, const std::filesystem::path & path);

/**
 * Connect to a Unix domain socket.
 */
void connect(Socket fd, const std::filesystem::path & path);

/**
 * Connect to a Unix domain socket.
 */
AutoCloseFD connect(const std::filesystem::path & path);

#ifndef _WIN32
namespace unix {

/**
 * Send a message with file descriptors over a Unix domain socket using
 * sendmsg with SCM_RIGHTS.
 *
 * @param sockfd The socket file descriptor to send the message on
 * @param data The data to send
 * @param fds A span of file descriptors to pass via SCM_RIGHTS
 *
 * @throws SysError on failure
 */
void sendMessageWithFds(Descriptor sockfd, std::string_view data, std::span<const Descriptor> fds);

/**
 * Result of receiving a message with file descriptors.
 */
struct ReceivedMessage
{
    /**
     * Number of bytes received into the data buffer
     */
    size_t bytesReceived;
    /**
     * The file descriptors received via SCM_RIGHTS, wrapped in
     * AutoCloseFD for RAII.
     */
    std::vector<AutoCloseFD> fds;
};

/**
 * Receive a message with file descriptors over a Unix domain socket
 * using recvmsg with SCM_RIGHTS.
 *
 * All file descriptors associated with the message will be returned. This
 * avoids unrecoverably dropping file descriptors with a messages. This is why
 * a vector is returned, as opposed to the caller passing in a `std::span` with
 * length of their choosing, as that may not be long enough.
 *
 * @param sockfd The socket file descriptor to receive the message on
 * @param data Buffer to receive data into
 *
 * @return A ReceivedMessage containing the bytes received count and file descriptors
 * @throws SysError on failure
 * @throws EndOfFile if the connection is closed
 */
ReceivedMessage receiveMessageWithFds(Descriptor sockfd, std::span<std::byte> data);

} // namespace unix
#endif

} // namespace nix
