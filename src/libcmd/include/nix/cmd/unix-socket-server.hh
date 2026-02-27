#pragma once
///@file

#include "nix/util/file-descriptor.hh"

#include <filesystem>
#include <functional>
#include <optional>
#include <sys/types.h>

namespace nix::unix {

/**
 * Information about the identity of the peer on a Unix domain socket connection.
 */
struct PeerInfo
{
    std::optional<pid_t> pid;
    std::optional<uid_t> uid;
    std::optional<gid_t> gid;
};

/**
 * Get the identity of the caller, if possible.
 */
PeerInfo getPeerInfo(Descriptor remote);

/**
 * Callback type for handling new connections.
 *
 * The callback receives ownership of the connection and is responsible
 * for handling it (e.g., forking a child process, spawning a thread, etc.).
 *
 * @param socket The accepted connection file descriptor.
 * @param closeListeners A callback to close the listening sockets.
 *   Useful in forked child processes to release the bound sockets.
 */
using UnixSocketHandler = fun<void(AutoCloseFD socket, std::function<void()> closeListeners)>;

/**
 * Options for the serve loop.
 *
 * Only used if no systemd socket activation is detected.
 */
struct ServeUnixSocketOptions
{
    /**
     * The Unix domain socket path to create and listen on.
     */
    std::filesystem::path socketPath;

    /**
     * Mode for the created socket file.
     */
    mode_t socketMode = 0666;
};

/**
 * Run a server loop that accepts connections and calls the handler for each.
 *
 * This function handles:
 * - systemd socket activation (via LISTEN_FDS environment variable)
 * - Creating and binding a Unix domain socket if no activation is detected
 * - Polling for incoming connections
 * - Accepting connections
 *
 * For each accepted connection, the handler is called with the connection
 * file descriptor. The handler takes ownership of the file descriptor and
 * is responsible for closing it when done.
 *
 * This function never returns normally. It runs until interrupted
 * (e.g., via SIGINT), at which point it throws `Interrupted`.
 *
 * @param options Configuration for the server.
 * @param handler Callback invoked for each accepted connection.
 */
[[noreturn]] void serveUnixSocket(const ServeUnixSocketOptions & options, UnixSocketHandler handler);

} // namespace nix::unix
