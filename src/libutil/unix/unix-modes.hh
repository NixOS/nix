#pragma once

#include <fcntl.h>

namespace nix::unix {

/**
 * Access mode for opening a directory only to traverse it: to use it as
 * the base of `*at` calls, or to `fstat` it. That needs search
 * permission but not read permission, as long as the OS lets us ask for
 * just that.
 *
 * @see nix::openDirectoryTraversable()
 */
constexpr int traversableDirAccessMode =
#if defined(O_SEARCH)
    /* The POSIX way to ask for exactly this. */
    O_SEARCH
#elif defined(O_PATH)
    /* Linux (glibc) lacks `O_SEARCH`, but `O_PATH` suffices. */
    O_PATH
#else
    /* Sadly this requires read permission, but without `O_SEARCH` or
       `O_PATH` that's unavoidable. */
    O_RDONLY
#endif
    ;

/**
 * Whether @ref traversableDirAccessMode really avoids needing read
 * permission, i.e. whether `openDirectoryTraversable()` can succeed
 * where `openDirectory()` fails with `EACCES`. If not, there is no point
 * retrying with the former.
 */
constexpr bool canTraverseWithoutRead = traversableDirAccessMode != O_RDONLY;

} // namespace nix::unix
