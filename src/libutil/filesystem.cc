#include <sys/time.h>
#include <filesystem>

#include "util.hh"
#include "types.hh"

namespace fs = std::filesystem;

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

        moveFile(tmp, link);

        break;
    }
}

void setWriteTime(const fs::path & p, const struct stat & st)
{
    struct timeval times[2];
    times[0] = {
        .tv_sec = st.st_atime,
        .tv_usec = 0,
    };
    times[1] = {
        .tv_sec = st.st_mtime,
        .tv_usec = 0,
    };
    warn("Setting the mtime of %s to %d", p.c_str(), st.st_mtim.tv_sec);
    if (lutimes(p.c_str(), times) != 0)
        throw SysError("changing modification time of '%s'", p);
}

void copy(const fs::directory_entry & from, const fs::path & to, bool andDelete)
{
    // TODO: Rewrite the `is_*` to use `symlink_status()`
    auto statOfFrom = lstat(from.path().c_str());
    auto fromStatus = from.symlink_status();

    // Mark the directory as writable so that we can delete its children
    if (andDelete && fs::is_directory(fromStatus)) {
        fs::permissions(from.path(), fs::perms::owner_write, fs::perm_options::add | fs::perm_options::nofollow);
    }


    if (fs::is_symlink(fromStatus) || fs::is_regular_file(fromStatus)) {
        fs::copy(from.path(), to, fs::copy_options::copy_symlinks | fs::copy_options::overwrite_existing);
    } else if (fs::is_directory(fromStatus)) {
        fs::create_directory(to);
        for (auto & entry : fs::directory_iterator(from.path())) {
            copy(entry, to / entry.path().filename(), andDelete);
        }
    } else {
        throw Error("file '%s' has an unsupported type", from.path());
    }

    setWriteTime(to, statOfFrom);
    if (andDelete) {
        if (!fs::is_symlink(fromStatus))
            fs::permissions(from.path(), fs::perms::owner_write, fs::perm_options::add | fs::perm_options::nofollow);
        fs::remove(from.path());
    }
}

void moveFile(const Path & oldName, const Path & newName)
{
    auto oldPath = fs::path(oldName);
    auto newPath = fs::path(newName);
    try {
        fs::rename(oldPath, newPath);
    } catch (fs::filesystem_error & e) {
        if (e.code().value() == EXDEV) {
            fs::remove(newPath);
            warn("Copying %s to %s", oldName, newName);
            copy(fs::directory_entry(oldPath), newPath, true);
        }
    }
}

}
