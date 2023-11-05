#pragma once
///@file

#include "types.hh"
#include "file-descriptor.hh"

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
 * Bind a Unix domain socket to a path.
 */
void bind(int fd, const std::string & path);

/**
 * Connect to a Unix domain socket.
 */
void connect(int fd, const std::string & path);

}
