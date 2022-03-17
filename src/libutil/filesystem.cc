#include <sys/time.h>

#include "util.hh"
#include "types.hh"

namespace nix {

void createSymlink(const Path & target, const Path & link,
    std::optional<time_t> mtime)
{
    if (symlink(target.c_str(), link.c_str()))
        throw SysError("creating symlink from '%1%' to '%2%'", link, target);
    if (mtime) {
        struct timeval times[2];
        times[0].tv_sec = *mtime;
        times[0].tv_usec = 0;
        times[1].tv_sec = *mtime;
        times[1].tv_usec = 0;
        if (lutimes(link.c_str(), times))
            throw SysError("setting time of symlink '%s'", link);
    }
}

void replaceSymlink(const Path & target, const Path & link,
    std::optional<time_t> mtime)
{
    for (unsigned int n = 0; true; n++) {
        Path tmp = canonPath(fmt("%s/.%d_%s", dirOf(link), n, baseNameOf(link)));

        try {
            createSymlink(target, tmp, mtime);
        } catch (SysError & e) {
            if (e.errNo == EEXIST) continue;
            throw;
        }

        if (rename(tmp.c_str(), link.c_str()) != 0)
            throw SysError("renaming '%1%' to '%2%'", tmp, link);

        break;
    }
}

}
