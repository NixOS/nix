#pragma once

namespace nix {

struct LocalSettings;

/**
 * Set up seccomp syscall filtering for the build process.
 * This prevents builders from creating setuid/setgid binaries
 * and from using extended attributes or ACLs.
 */
void setupSeccomp(const LocalSettings & localSettings);

} // namespace nix
