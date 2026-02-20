#include "nix/util/file-system.hh"
#include "nix/util/logging.hh"
#include "nix/util/signals.hh"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <boost/format.hpp>

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

AutoCloseFD openDirectory(const std::filesystem::path & path)
{
    return AutoCloseFD{CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        /*lpSecurityAttributes=*/nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        /*hTemplateFile=*/nullptr)};
}

AutoCloseFD openFileReadonly(const std::filesystem::path & path)
{
    return AutoCloseFD{CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        /*lpSecurityAttributes=*/nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        /*hTemplateFile=*/nullptr)};
}

AutoCloseFD
openNewFileForWrite(const std::filesystem::path & path, [[maybe_unused]] mode_t mode, OpenNewFileForWriteParams params)
{
    return AutoCloseFD{CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        /*lpSecurityAttributes=*/nullptr,
        params.truncateExisting ? CREATE_ALWAYS : CREATE_NEW, /* TODO: Reparse points. */
        FILE_ATTRIBUTE_NORMAL,
        /*hTemplateFile=*/nullptr)};
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

std::filesystem::path descriptorToPath(Descriptor handle)
{
    std::vector<wchar_t> buf(0x100);
    DWORD dw = GetFinalPathNameByHandleW(handle, buf.data(), buf.size(), FILE_NAME_OPENED);
    if (dw == 0) {
        if (handle == GetStdHandle(STD_INPUT_HANDLE))
            return L"<stdin>";
        if (handle == GetStdHandle(STD_OUTPUT_HANDLE))
            return L"<stdout>";
        if (handle == GetStdHandle(STD_ERROR_HANDLE))
            return L"<stderr>";
        return (boost::wformat(L"<unnnamed handle %X>") % handle).str();
    }
    if (dw > buf.size()) {
        buf.resize(dw);
        if (GetFinalPathNameByHandleW(handle, buf.data(), buf.size(), FILE_NAME_OPENED) != dw - 1)
            throw WinError("GetFinalPathNameByHandleW");
        dw -= 1;
    }
    return std::filesystem::path{std::wstring{buf.data(), dw}};
}

} // namespace nix
