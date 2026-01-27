#pragma once
///@file

#include "nix/util/file-descriptor.hh"

#ifdef _WIN32
#  include <winsock2.h>
#endif

namespace nix {

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
 * Convert a `Descriptor` to a `Socket`
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

} // namespace nix
