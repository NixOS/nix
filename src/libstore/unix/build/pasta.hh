#pragma once

#ifdef __linux__

#  include "nix/util/processes.hh"
#  include "nix/util/file-descriptor.hh"
#  include <string>
#  include <set>

namespace nix {

/**
 * Pasta (Plug A Simple Socket Transport) network isolation constants and utilities.
 *
 * Pasta provides network isolation for fixed-output derivations by creating
 * a Layer-2 to Layer-4 translation without requiring special privileges.
 */
namespace pasta {

// Network configuration constants
// NOTE: These are all C strings because macOS doesn't have constexpr std::string
// constructors, and std::string_view is a pain to turn into std::strings again.
static constexpr const char * PASTA_NS_IFNAME = "eth0";
static constexpr const char * PASTA_HOST_IPV4 = "169.254.1.1";
static constexpr const char * PASTA_CHILD_IPV4 = "169.254.1.2";
static constexpr const char * PASTA_IPV4_NETMASK = "16";
// Randomly chosen 6to4 prefix, mapping the same ipv4ll as above.
// Even if this id is used on the daemon host there should not be
// any collisions since ipv4ll should never be addressed by ipv6.
static constexpr const char * PASTA_HOST_IPV6 = "64:ff9b:1:4b8e:472e:a5c8:a9fe:0101";
static constexpr const char * PASTA_CHILD_IPV6 = "64:ff9b:1:4b8e:472e:a5c8:a9fe:0102";

/**
 * Launch pasta for network isolation of a build process.
 *
 * @param pastaPath Path to the pasta executable
 * @param pid Process ID of the build process to isolate
 * @param buildUserId Optional UID to run pasta as
 * @param buildGroupId Optional GID to run pasta as
 * @param usingUserNamespace Whether the build is using a user namespace
 * @return Process ID of the pasta process
 */
Pid setupPasta(
    const Path & pastaPath,
    pid_t pid,
    std::optional<uid_t> buildUserId,
    std::optional<gid_t> buildGroupId,
    bool usingUserNamespace);

/**
 * Wait for pasta to set up the network interface.
 *
 * @throws Error if the interface doesn't appear within the timeout period
 */
void waitForPastaInterface();

/**
 * Rewrite /etc/resolv.conf for pasta-isolated builds.
 *
 * Replaces nameserver entries with pasta's DNS forwarders.
 *
 * @param fromHost The original resolv.conf content from the host
 * @return Modified resolv.conf content for the sandboxed build
 */
std::string rewriteResolvConf(const std::string & fromHost);

/**
 * Kill the pasta process.
 *
 * @param pastaPid The pasta process to kill
 * @throws Error if pasta exits with unexpected status
 */
void killPasta(Pid & pastaPid);

} // namespace pasta
} // namespace nix

#endif // __linux__