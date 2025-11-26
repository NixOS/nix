#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/socket.hh"

#include <unistd.h>

#include <filesystem>

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

} // namespace nix
