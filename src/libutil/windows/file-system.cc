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

Descriptor openDirectory(const std::filesystem::path & path, bool followFinalSymlink)
{
    return CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        /*lpSecurityAttributes=*/nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | (followFinalSymlink ? 0 : FILE_FLAG_OPEN_REPARSE_POINT),
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

Descriptor
openNewFileForWrite(const std::filesystem::path & path, [[maybe_unused]] mode_t mode, OpenNewFileForWriteParams params)
{
    return CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        /*lpSecurityAttributes=*/nullptr,
        params.truncateExisting ? CREATE_ALWAYS : CREATE_NEW, /* TODO: Reparse points. */
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

/**
 * Convert Windows FILETIME to Unix time_t.
 */
static time_t fileTimeToUnixTime(const FILETIME & ft)
{
    /* FILETIME is 100-nanosecond intervals since January 1, 1601 UTC.
       Unix time is seconds since January 1, 1970 UTC.
       Difference is 11644473600 seconds. */
    ULARGE_INTEGER ull;
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;
    return static_cast<time_t>(ull.QuadPart / 10000000ULL - 11644473600ULL);
}

/**
 * Fill a PosixStat structure from WIN32_FIND_DATAW.
 * This gives us the file's own metadata without following symlinks.
 */
static void statFromFindData(const WIN32_FIND_DATAW & fd, PosixStat & st)
{
    memset(&st, 0, sizeof(st));

    /* Determine file type */
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        st.st_mode = S_IFLNK | 0777;
    } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        st.st_mode = S_IFDIR | 0755;
    } else {
        st.st_mode = S_IFREG | 0644;
    }

    /* File size (only meaningful for regular files) */
    st.st_size = (static_cast<int64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;

    /* Timestamps */
    st.st_atime = fileTimeToUnixTime(fd.ftLastAccessTime);
    st.st_mtime = fileTimeToUnixTime(fd.ftLastWriteTime);
    st.st_ctime = fileTimeToUnixTime(fd.ftCreationTime);

    /* Windows doesn't have these concepts, set to reasonable defaults */
    st.st_nlink = 1;
    st.st_uid = 0;
    st.st_gid = 0;
}

PosixStat lstat(const std::filesystem::path & path)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(path.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        throw WinError("getting status of %s", PathFmt(path));
    FindClose(h);

    PosixStat st;
    statFromFindData(fd, st);
    return st;
}

std::optional<PosixStat> maybeLstat(const std::filesystem::path & path)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(path.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            return std::nullopt;
        throw WinError(err, "getting status of %s", PathFmt(path));
    }
    FindClose(h);

    PosixStat st;
    statFromFindData(fd, st);
    return st;
}

} // namespace nix
