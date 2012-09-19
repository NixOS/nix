#include "config.h"

#include "immutable.hh"
#include "util.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if HAVE_LINUX_FS_H
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <errno.h>
#endif

namespace nix {


void makeMutable(const Path & path)
{
#if defined(FS_IOC_SETFLAGS) && defined(FS_IOC_GETFLAGS) && defined(FS_IMMUTABLE_FL)

    /* Don't even try if we're not root.  One day we should support
       the CAP_LINUX_IMMUTABLE capability. */
    if (getuid() != 0) return;

    /* The O_NOFOLLOW is important to prevent us from changing the
       mutable bit on the target of a symlink (which would be a
       security hole). */
    AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd == -1) {
        if (errno == ELOOP) return; // it's a symlink
        throw SysError(format("opening file `%1%'") % path);
    }

    unsigned int flags = 0, old;

    /* Silently ignore errors getting/setting the immutable flag so
       that we work correctly on filesystems that don't support it. */
    if (ioctl(fd, FS_IOC_GETFLAGS, &flags)) return;
    old = flags;
    flags &= ~FS_IMMUTABLE_FL;
    if (old == flags) return;
    if (ioctl(fd, FS_IOC_SETFLAGS, &flags)) return;
#endif
}


}
