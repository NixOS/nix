#include "nix/file-system.hh"

#ifdef _WIN32
namespace nix {

namespace fs {
using namespace std::filesystem;
}

void setWriteTime(const fs::path & path, time_t accessedTime, time_t modificationTime, std::optional<bool> optIsSymlink)
{
    // FIXME use `fs::last_write_time`.
    //
    // Would be nice to use std::filesystem unconditionally, but
    // doesn't support access time just modification time.
    //
    // System clock vs File clock issues also make that annoying.
    warn("Changing file times is not yet implemented on Windows, path is %s", path);
}

Descriptor openDirectory(const std::filesystem::path & path)
{
    return CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);
}

}
#endif
