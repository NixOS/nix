#include "nix/util/file-system.hh"
#include "nix/util/logging.hh"

namespace nix {

using namespace nix::windows;

void setWriteTime(
    const std::filesystem::path & path, time_t accessedTime, time_t modificationTime, std::optional<bool> optIsSymlink)
{
    // FIXME use `std::filesystem::last_write_time`.
    //
    // Would be nice to use std::filesystem unconditionally, but
    // doesn't support access time just modification time.
    //
    // System clock vs File clock issues also make that annoying.
    warn("Changing file times is not yet implemented on Windows, path is %s", PathFmt(path));
}

Descriptor openDirectory(const std::filesystem::path & path)
{
    return CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        /*lpSecurityAttributes=*/nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        /*hTemplateFile=*/nullptr);
}

Descriptor openFileReadonly(const std::filesystem::path & path)
{
    return CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        /*lpSecurityAttributes=*/nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        /*hTemplateFile=*/nullptr);
}

std::filesystem::path defaultTempDir()
{
    wchar_t buf[MAX_PATH + 1];
    DWORD len = GetTempPathW(MAX_PATH + 1, buf);
    if (len == 0 || len > MAX_PATH)
        throw WinError("getting default temporary directory");
    return std::filesystem::path(buf);
}

void deletePath(const std::filesystem::path & path)
{
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory)
        throw SysError(ec.default_error_condition().value(), "recursively deleting %1%", PathFmt(path));
}

void deletePath(const std::filesystem::path & path, uint64_t & bytesFreed)
{
    bytesFreed = 0;
    deletePath(path);
}

} // namespace nix
