#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#include "nix/util/file-system.hh"

#include "util-unix-config-private.hh"

namespace nix {

Descriptor openDirectory(const std::filesystem::path & path)
{
    return open(path.c_str(), O_RDONLY | O_DIRECTORY);
}

void setWriteTime(
    const std::filesystem::path & path, time_t accessedTime, time_t modificationTime, std::optional<bool> optIsSymlink)
{
    // Would be nice to use std::filesystem unconditionally, but
    // doesn't support access time just modification time.
    //
    // System clock vs File clock issues also make that annoying.
#if HAVE_UTIMENSAT && HAVE_DECL_AT_SYMLINK_NOFOLLOW
    struct timespec times[2] = {
        {
            .tv_sec = accessedTime,
            .tv_nsec = 0,
        },
        {
            .tv_sec = modificationTime,
            .tv_nsec = 0,
        },
    };
    if (utimensat(AT_FDCWD, path.c_str(), times, AT_SYMLINK_NOFOLLOW) == -1)
        throw SysError("changing modification time of %s (using `utimensat`)", path);
#else
    struct timeval times[2] = {
        {
            .tv_sec = accessedTime,
            .tv_usec = 0,
        },
        {
            .tv_sec = modificationTime,
            .tv_usec = 0,
        },
    };
#  if HAVE_LUTIMES
    if (lutimes(path.c_str(), times) == -1)
        throw SysError("changing modification time of %s", path);
#  else
    bool isSymlink = optIsSymlink ? *optIsSymlink : std::filesystem::is_symlink(path);

    if (!isSymlink) {
        if (utimes(path.c_str(), times) == -1)
            throw SysError("changing modification time of %s (not a symlink)", path);
    } else {
        throw Error("Cannot change modification time of symlink %s", path);
    }
#  endif
#endif
}

} // namespace nix
