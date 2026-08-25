#include "nix/util/file-system.hh"
#include "nix/util/logging.hh"
#include "nix/util/signals.hh"
#include "nix/util/canon-path.hh"
#include "nix/util/util.hh"
#include "file-system-at-private.hh"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <boost/format.hpp>
#include <chrono>

// Verify that our S_IFLNK polyfill doesn't conflict with Windows file type constants
static_assert((S_IFLNK & S_IFMT) == S_IFLNK, "S_IFLNK must fit within S_IFMT mask");
static_assert(S_IFLNK != S_IFDIR, "S_IFLNK must not equal S_IFDIR");
static_assert(S_IFLNK != S_IFREG, "S_IFLNK must not equal S_IFREG");
static_assert(S_IFLNK != S_IFCHR, "S_IFLNK must not equal S_IFCHR");

namespace nix {

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

AutoCloseFD openFileReadonly(const std::filesystem::path & path, FinalSymlink finalSymlink)
{
    return AutoCloseFD{CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        /*lpSecurityAttributes=*/nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | (finalSymlink == FinalSymlink::Follow ? 0 : FILE_FLAG_OPEN_REPARSE_POINT),
        /*hTemplateFile=*/nullptr)};
}

AutoCloseFD
openNewFileForWrite(const std::filesystem::path & path, [[maybe_unused]] mode_t mode, OpenNewFileForWriteParams params)
{
    return AutoCloseFD{CreateFileW(
        path.c_str(),
        GENERIC_WRITE | (params.writeOnly ? 0 : GENERIC_READ),
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
        throw windows::WinError("getting default temporary directory");
    return std::filesystem::path(buf);
}

namespace {

/**
 * Clear `FILE_ATTRIBUTE_READONLY` through an already-open handle.
 *
 * A file carrying it cannot be deleted, and the store is full of them:
 * canonicalisation chmods store contents to 0444, and `chmod()` on Windows is
 * `::_wchmod`, which turns a missing write bit into exactly this attribute.
 *
 * This is the counterpart of the Unix walk relaxing permissions with
 * `fchmodatTryNoFollow` before it recurses. Doing it through the handle rather
 * than by path means the object whose attribute is cleared is necessarily the
 * one about to be deleted.
 *
 * The attribute is not honoured on directories, so in practice this only
 * matters for files, but it is harmless to apply uniformly.
 */
void clearReadOnly(Descriptor fd)
{
    FILE_BASIC_INFO basic;
    if (!GetFileInformationByHandleEx(fd, FileBasicInfo, &basic, sizeof(basic)))
        return; /* Leave it; the deletion below will report the real problem. */

    if (!(basic.FileAttributes & FILE_ATTRIBUTE_READONLY))
        return;

    basic.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
    /* Clearing the last attribute leaves zero, which is not a valid value to
       set; `FILE_ATTRIBUTE_NORMAL` is how you say "no attributes". */
    if (basic.FileAttributes == 0)
        basic.FileAttributes = FILE_ATTRIBUTE_NORMAL;

    SetFileInformationByHandle(fd, FileBasicInfo, &basic, sizeof(basic));
}

/**
 * Delete through an already-open handle, so the name is never resolved twice.
 *
 * This marks the object for deletion on last-handle-close rather than unlinking
 * the name immediately. `FileDispositionInfoEx` with
 * `FILE_DISPOSITION_FLAG_POSIX_SEMANTICS` would do the latter, but it needs a
 * newer API level than the `_WIN32_WINNT=0x0602` this project sets, and the
 * distinction does not matter here: the caller closes the handle before moving
 * on, so a child's name is gone before its parent is deleted.
 *
 * @return whether the object was marked for deletion.
 */
bool deleteByHandle(Descriptor fd)
{
    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    return SetFileInformationByHandle(fd, FileDispositionInfo, &disposition, sizeof(disposition));
}

/**
 * List a directory through its own handle.
 *
 * The names are collected rather than acted on as they arrive, because deleting
 * entries while an enumeration of the same directory is in flight is not
 * defined to visit each entry exactly once.
 */
std::vector<std::wstring> listByHandle(Descriptor fd, const std::filesystem::path & path)
{
    std::vector<std::wstring> names;

    /* Big enough that a typical directory needs one round trip, but the loop
       below does not depend on that. */
    std::vector<char> buf(64 * 1024);

    while (true) {
        checkInterrupt();

        if (!GetFileInformationByHandleEx(fd, FileFullDirectoryInfo, buf.data(), buf.size())) {
            auto lastError = GetLastError();
            if (lastError == ERROR_NO_MORE_FILES)
                break;
            throw windows::WinError(lastError, "reading directory %1%", PathFmt(path));
        }

        auto * info = reinterpret_cast<FILE_FULL_DIR_INFO *>(buf.data());
        while (true) {
            std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
            if (name != L"." && name != L"..")
                names.push_back(std::move(name));
            if (info->NextEntryOffset == 0)
                break;
            info = reinterpret_cast<FILE_FULL_DIR_INFO *>(reinterpret_cast<char *>(info) + info->NextEntryOffset);
        }
    }

    return names;
}

/**
 * Recursively delete `name` within the directory `parentFd` refers to.
 *
 * Mirrors the Unix `_deletePath`: every step is relative to a directory handle,
 * so no path is resolved a second time and there is no window in which a
 * component could be replaced. Reparse points are opened rather than followed,
 * so a symlink is removed as a link and its target is left alone.
 *
 * Errors deleting individual entries are collected in `ex` rather than thrown
 * immediately, so that one undeletable entry does not abandon the rest of the
 * tree. This too matches Unix.
 */
void deletePathAt(
    Descriptor parentFd, const std::filesystem::path & path, uint64_t & bytesFreed, std::exception_ptr & ex)
{
    checkInterrupt();

    auto name = path.filename().native();

    /* One handle, carrying everything the rest of this function needs:
       classification, listing, clearing the attribute, and the deletion. Two
       opens would mean resolving `name` twice, which is the race this is
       written to avoid. */
    auto fd = windows::tryNtOpenAt(
        parentFd,
        name,
        DELETE | FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE,
        FILE_OPEN_REPARSE_POINT);
    if (!fd)
        return; /* Already gone. */

    FILE_BASIC_INFO basic;
    if (!GetFileInformationByHandleEx(fd->get(), FileBasicInfo, &basic, sizeof(basic)))
        throw windows::WinError("getting attributes of %1%", PathFmt(path));

    bool isDir = (basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    bool isReparsePoint = (basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

    if (!isDir) {
        /* Will deleting this actually free space? Same policy as Unix: count it
           at one or two links, nothing at three or more, on the assumption that
           two means an optimised store entry. */
        FILE_STANDARD_INFO standard;
        if (GetFileInformationByHandleEx(fd->get(), FileStandardInfo, &standard, sizeof(standard))
            && standard.NumberOfLinks <= 2)
            bytesFreed += static_cast<uint64_t>(standard.EndOfFile.QuadPart);
    }

    /* Descend into real directories only. A directory symlink or junction is
       deleted as itself. */
    if (isDir && !isReparsePoint)
        for (auto & child : listByHandle(fd->get(), path))
            deletePathAt(fd->get(), path / child, bytesFreed, ex);

    clearReadOnly(fd->get());

    if (!deleteByHandle(fd->get())) {
        auto lastError = GetLastError();
        if (lastError == ERROR_FILE_NOT_FOUND || lastError == ERROR_PATH_NOT_FOUND)
            return;
        try {
            throw windows::WinError(lastError, "cannot delete %1%", PathFmt(path));
        } catch (...) {
            if (!ex)
                ex = std::current_exception();
            else
                ignoreExceptionExceptInterrupt();
        }
    }
}

} // namespace

void deletePath(const std::filesystem::path & path, uint64_t & bytesFreed)
{
    bytesFreed = 0;

    /* An empty path is a no-op. The `std::filesystem::remove_all` this replaces
       treated it as one, and callers depend on that: `nix-fetchers-tests` reaches
       here with an empty path while tearing down a skipped test. */
    if (path.empty())
        return;

    /* Resolve rather than assert. `is_absolute()` on Windows is
       `has_root_name() && has_root_directory()`, so a relative path -- or a
       POSIX-rooted one like `/tmp/x` -- is not absolute even when it names a real
       file, and `remove_all` accepted those too. */
    auto absPath = path.is_absolute() ? path : std::filesystem::absolute(path);

    auto parentPath = absPath.parent_path();
    assert(parentPath != absPath);

    auto parentFd = openDirectory(parentPath);
    if (!parentFd) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)
            return;
        throw windows::WinError("opening directory %1%", PathFmt(parentPath));
    }

    std::exception_ptr ex;

    deletePathAt(parentFd.get(), absPath, bytesFreed, ex);

    if (ex)
        std::rethrow_exception(ex);
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
            throw windows::WinError("GetFinalPathNameByHandleW");
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
    /* https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/stat-functions?view=msvc-170
       matches the regular windows stat functions. */
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
        throw windows::WinError("getting status of %s", PathFmt(path));
    return statFromFileInfo(attrData);
}

std::optional<PosixStat> maybeLstat(const std::filesystem::path & path)
{
    WIN32_FILE_ATTRIBUTE_DATA attrData;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attrData)) {
        auto lastError = GetLastError();
        if (lastError == ERROR_FILE_NOT_FOUND || lastError == ERROR_PATH_NOT_FOUND)
            return std::nullopt;
        throw windows::WinError(lastError, "getting status of %s", PathFmt(path));
    }
    return statFromFileInfo(attrData);
}

void movePath(const std::filesystem::path & src, const std::filesystem::path & dst)
{
    renameFile(src, dst);
}

void renameFile(const std::filesystem::path & src, const std::filesystem::path & dst)
{
    /* TODO: FileRenameInformationEx with FILE_RENAME_POSIX_SEMANTICS? */
    if (!::MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING))
        throw windows::WinError("renaming %1% to %2%", PathFmt(src), PathFmt(dst));
}

void createDir(const std::filesystem::path & path, [[maybe_unused]] mode_t mode)
{
    if (!::CreateDirectoryW(path.c_str(), nullptr))
        throw windows::WinError("creating directory %s", PathFmt(path));
}

} // namespace nix
