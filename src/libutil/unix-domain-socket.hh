#pragma once
///@file

#include "types.hh"
#include "file-descriptor.hh"

#ifdef _WIN32
#  include <winsock2.h>
#endif
#include <unistd.h>

namespace nix {

/**
 * Create a Unix domain socket.
 */
AutoCloseFD createUnixDomainSocket();

/**
 * Create a Unix domain socket in listen mode.
 */
AutoCloseFD createUnixDomainSocket(const Path & path, mode_t mode);

/**
 * Often we want to use `Descriptor`, but Windows makes a slightly
 * stronger file descriptor vs socket distinction, at least at the level
 * of C types.
 */
using Socket =
#ifdef _WIN32
    SOCKET
#else
    int
#endif
    ;

#ifdef _WIN32
/**
 * Windows gives this a different name
 */
#  define SHUT_WR SD_SEND
#  define SHUT_RDWR SD_BOTH
#endif

/**
 * Convert a `Socket` to a `Descriptor`
 *
 * This is a no-op except on Windows.
 */
static inline Socket toSocket(Descriptor fd)
{
#ifdef _WIN32
    return reinterpret_cast<Socket>(fd);
#else
    return fd;
#endif
}

/**
 * Convert a `Socket` to a `Descriptor`
 *
 * This is a no-op except on Windows.
 */
static inline Descriptor fromSocket(Socket fd)
{
#ifdef _WIN32
    return reinterpret_cast<Descriptor>(fd);
#else
    return fd;
#endif
}

/**
 * Bind a Unix domain socket to a path.
 */
void bind(Socket fd, const std::string & path);

/**
 * Connect to a Unix domain socket.
 */
void connect(Socket fd, const std::string & path);

}
