#pragma once

#include "os-string.hh"

namespace nix {

/**
 * `execvpe` is a GNU extension, so we need to implement it for other POSIX
 * platforms.
 *
 * We use our own implementation unconditionally for consistency.
 */
int execvpe(
    const OsString::value_type * file0,
    const OsString::value_type * const argv[],
    const OsString::value_type * const envp[]);

}
