#include "nix/util/file-system.hh"
#include "nix/util/logging.hh"
#include "nix/util/signals.hh"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <boost/format.hpp>
#include <chrono>

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

AutoCloseFD openDirectory(const std::filesystem::path & path, FinalSymlink finalSymlink)
{
    return AutoCloseFD{CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        /*lpSecurityAttributes=*/nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | (finalSymlink == FinalSymlink::Follow ? 0 : FILE_FLAG_OPEN_REPARSE_POINT),
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

time_t windows::fileTimeToUnixTime(const FILETIME & ft)
{
    ULARGE_INTEGER ull;
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;

    // file_clock on Windows matches FILETIME's epoch (1601-01-01) and resolution (100ns)
    auto fileTP = std::chrono::file_clock::time_point{std::chrono::file_clock::duration{ull.QuadPart}};

    auto sysTP = std::chrono::clock_cast<std::chrono::system_clock>(fileTP);
    return std::chrono::system_clock::to_time_t(sysTP);
}

void windows::statFromFileInfo(
    PosixStat & st,
    DWORD dwFileAttributes,
    const FILETIME & ftCreationTime,
    const FILETIME & ftLastAccessTime,
    const FILETIME & ftLastWriteTime,
    DWORD nFileSizeHigh,
    DWORD nFileSizeLow,
    DWORD nNumberOfLinks)
{
    memset(&st, 0, sizeof(st));

    /* Determine file type */
    if (dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        st.st_mode = S_IFLNK | 0777;
    } else if (dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        st.st_mode = S_IFDIR | 0755;
    } else {
        st.st_mode = S_IFREG | 0644;
    }

    /* File size (only meaningful for regular files) */
    st.st_size = (static_cast<int64_t>(nFileSizeHigh) << 32) | nFileSizeLow;

    /* Timestamps */
    st.st_atime = fileTimeToUnixTime(ftLastAccessTime);
    st.st_mtime = fileTimeToUnixTime(ftLastWriteTime);
    st.st_ctime = fileTimeToUnixTime(ftCreationTime);

    st.st_nlink = nNumberOfLinks;
    st.st_uid = 0;
    st.st_gid = 0;
}

static PosixStat statFromFileInfo(const WIN32_FILE_ATTRIBUTE_DATA & attrData)
{
    PosixStat st;
    windows::statFromFileInfo(
        st,
        attrData.dwFileAttributes,
        attrData.ftCreationTime,
        attrData.ftLastAccessTime,
        attrData.ftLastWriteTime,
        attrData.nFileSizeHigh,
        attrData.nFileSizeLow);
    return st;
}

PosixStat lstat(const std::filesystem::path & path)
{
    WIN32_FILE_ATTRIBUTE_DATA attrData;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attrData))
        throw WinError("getting status of %s", PathFmt(path));
    return statFromFileInfo(attrData);
}

std::optional<PosixStat> maybeLstat(const std::filesystem::path & path)
{
    WIN32_FILE_ATTRIBUTE_DATA attrData;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attrData)) {
        auto lastError = GetLastError();
        if (lastError == ERROR_FILE_NOT_FOUND || lastError == ERROR_PATH_NOT_FOUND)
            return std::nullopt;
        throw WinError(lastError, "getting status of %s", PathFmt(path));
    }
    return statFromFileInfo(attrData);
}

} // namespace nix
