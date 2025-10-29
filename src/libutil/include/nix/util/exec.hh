#pragma once

#include "nix/util/os-string.hh"

namespace nix {

/**
 * `execvpe` is a GNU extension, so we need to implement it for other POSIX
 * platforms.
 *
 * We use our own implementation unconditionally for consistency.
 */
int execvpe(const OsChar * file0, const OsChar * const argv[], const OsChar * const envp[]);

} // namespace nix
