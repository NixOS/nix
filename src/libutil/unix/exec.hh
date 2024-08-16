#pragma once

namespace nix {

/**
 * `execvpe` is a GNU extension, so we need to implement it for other POSIX
 * platforms.
 *
 * We use our own implementation unconditionally for consistency.
 */
int execvpe(const char * file0, char * const argv[], char * const envp[]);

}
