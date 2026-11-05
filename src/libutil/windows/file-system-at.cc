#include "nix/util/file-system-at.hh"
#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"
#include "nix/util/file-path.hh"
#include "nix/util/source-accessor.hh"
#include "nix/util/util.hh"
#include "nix/util/system-error.hh"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <tuple>
#include <vector>

#include <fileapi.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <winternl.h>

namespace nix {

namespace windows {

namespace {

/**
 * Open a file/directory relative to a directory handle using NtCreateFile.
 *
 * @param dirFd Directory handle to open relative to
 * @param pathComponent Single path component (not a full path)
 * @param desiredAccess Access rights requested
 * @param createOptions NT create options flags
 * @param createDisposition FILE_OPEN, FILE_CREATE, etc.
 * @return Handle to the opened file/directory, or NTSTATUS on error
 */
Result<AutoCloseFD, NTSTATUS> maybeNtOpenAt(
    Descriptor dirFd,
    std::wstring_view pathComponent,
    ACCESS_MASK desiredAccess,
    ULONG createOptions,
    ULONG createDisposition = FILE_OPEN)
{
    /* Set up UNICODE_STRING for the relative path */
    UNICODE_STRING pathStr;
    pathStr.Buffer = const_cast<PWSTR>(pathComponent.data());
    pathStr.Length = static_cast<USHORT>(pathComponent.size() * sizeof(wchar_t));
    pathStr.MaximumLength = pathStr.Length;

    /* Set up OBJECT_ATTRIBUTES to open relative to dirFd */
    OBJECT_ATTRIBUTES objAttrs;
    InitializeObjectAttributes(
        &objAttrs,
        &pathStr,
        0,      // No special flags
        dirFd,  // RootDirectory
        nullptr // No security descriptor
    );

    /* Open using NT API */
    IO_STATUS_BLOCK ioStatus;
    HANDLE h;
    NTSTATUS status = NtCreateFile(
        &h,
        desiredAccess,
        &objAttrs,
        &ioStatus,
        nullptr, // No allocation size
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        createDisposition,
        createOptions | FILE_SYNCHRONOUS_IO_NONALERT,
        nullptr, // No EA buffer
        0        // No EA length
    );

    if (status != 0)
        return outcome::failure(status);

    return AutoCloseFD{h};
}

AutoCloseFD ntOpenAt(
    Descriptor dirFd,
    std::wstring_view pathComponent,
    ACCESS_MASK desiredAccess,
    ULONG createOptions,
    ULONG createDisposition = FILE_OPEN)
{
    auto result = maybeNtOpenAt(dirFd, pathComponent, desiredAccess, createOptions, createDisposition);
    if (!result)
        throw WinError(
            RtlNtStatusToDosError(result.error()), "opening %s relative to directory handle", PathFmt(pathComponent));
    return std::move(result.value());
}

/**
 * Open a symlink relative to a directory handle without following it.
 *
 * @param dirFd Directory handle to open relative to
 * @param path Relative path to the symlink
 * @return Handle to the symlink
 */
AutoCloseFD openSymlinkAt(Descriptor dirFd, const OsCanonPath & path)
{
    assert(!path.empty());

    return ntOpenAt(dirFd, path.c_str(), FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_OPEN_REPARSE_POINT);
}

/**
 * This struct isn't defined in the normal Windows SDK, but only in the Windows Driver Kit.
 *
 * I (@Ericson2314) would not normally do something like this, but LLVM
 * has decided that this is in fact stable, per
 * https://github.com/llvm/llvm-project/blob/main/libcxx/src/filesystem/posix_compat.h,
 * so I guess that is good enough for us. GCC doesn't support symlinks
 * at all on windows so we have to put it here, not grab it from private
 * c++ standard library headers anyways.
 */
struct ReparseDataBuffer
{
    unsigned long ReparseTag;
    unsigned short ReparseDataLength;
    unsigned short Reserved;

    union
    {
        struct
        {
            unsigned short SubstituteNameOffset;
            unsigned short SubstituteNameLength;
            unsigned short PrintNameOffset;
            unsigned short PrintNameLength;
            unsigned long Flags;
            wchar_t PathBuffer[1];
        } SymbolicLinkReparseBuffer;

        struct
        {
            unsigned short SubstituteNameOffset;
            unsigned short SubstituteNameLength;
            unsigned short PrintNameOffset;
            unsigned short PrintNameLength;
            wchar_t PathBuffer[1];
        } MountPointReparseBuffer;

        struct
        {
            unsigned char DataBuffer[1];
        } GenericReparseBuffer;
    };
};

/**
 * Read the target of a symlink from an open handle.
 *
 * @param linkHandle Handle to a symlink (must have been opened with FILE_OPEN_REPARSE_POINT)
 * @return The symlink target as a wide string
 */
OsString readSymlinkTarget(HANDLE linkHandle)
{
    uint8_t buf[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
    DWORD out;

    checkInterrupt();

    if (!DeviceIoControl(linkHandle, FSCTL_GET_REPARSE_POINT, nullptr, 0, buf, sizeof(buf), &out, nullptr))
        throw WinError("reading reparse point for handle %d", linkHandle);

    const auto * reparse = reinterpret_cast<const ReparseDataBuffer *>(buf);
    size_t path_buf_offset = offsetof(ReparseDataBuffer, SymbolicLinkReparseBuffer.PathBuffer[0]);

    if (out < path_buf_offset) {
        auto fullPath = descriptorToPath(linkHandle);
        throw WinError(
            DWORD{ERROR_REPARSE_TAG_INVALID}, "invalid reparse data for %d:%s", linkHandle, PathFmt(fullPath));
    }

    if (reparse->ReparseTag != IO_REPARSE_TAG_SYMLINK) {
        auto fullPath = descriptorToPath(linkHandle);
        throw WinError(DWORD{ERROR_REPARSE_TAG_INVALID}, "not a symlink: %d:%s", linkHandle, PathFmt(fullPath));
    }

    const auto & symlink = reparse->SymbolicLinkReparseBuffer;
    unsigned short name_offset, name_length;

    /* Prefer PrintName over SubstituteName if available */
    if (symlink.PrintNameLength == 0) {
        name_offset = symlink.SubstituteNameOffset;
        name_length = symlink.SubstituteNameLength;
    } else {
        name_offset = symlink.PrintNameOffset;
        name_length = symlink.PrintNameLength;
    }

    if (path_buf_offset + name_offset + name_length > out) {
        auto fullPath = descriptorToPath(linkHandle);
        throw WinError(
            DWORD{ERROR_REPARSE_TAG_INVALID}, "invalid symlink data for %d:%s", linkHandle, PathFmt(fullPath));
    }

    /* Extract the target path */
    const wchar_t * target_start = &symlink.PathBuffer[name_offset / sizeof(wchar_t)];
    size_t target_len = name_length / sizeof(wchar_t);

    return {target_start, target_len};
}

/**
 * Write symlink target to an open file handle using reparse point.
 *
 * @param handle Open file handle (must have GENERIC_WRITE access)
 * @param target The symlink target (what it points to)
 */
void writeSymlinkTarget(HANDLE handle, const std::filesystem::path & target)
{
    /* Build the reparse data buffer for a symbolic link.
       Layout: SubstituteName and PrintName stored consecutively in PathBuffer.
       We use the same string for both. */
    size_t targetBytes = target.native().size() * sizeof(wchar_t);
    size_t bufSize = offsetof(ReparseDataBuffer, SymbolicLinkReparseBuffer.PathBuffer) + targetBytes * 2;
    std::vector<uint8_t> buf(bufSize, 0);

    auto * reparse = reinterpret_cast<ReparseDataBuffer *>(buf.data());
    reparse->ReparseTag = IO_REPARSE_TAG_SYMLINK;
    reparse->ReparseDataLength =
        static_cast<unsigned short>(bufSize - offsetof(ReparseDataBuffer, SymbolicLinkReparseBuffer));
    reparse->Reserved = 0;

    auto & symlink = reparse->SymbolicLinkReparseBuffer;
    /* SubstituteName comes first */
    symlink.SubstituteNameOffset = 0;
    symlink.SubstituteNameLength = static_cast<unsigned short>(targetBytes);
    /* PrintName follows SubstituteName */
    symlink.PrintNameOffset = static_cast<unsigned short>(targetBytes);
    symlink.PrintNameLength = static_cast<unsigned short>(targetBytes);
    /* SYMLINK_FLAG_RELATIVE = 1 for relative symlinks, 0 for absolute */
    symlink.Flags = target.is_relative() ? 1 : 0;

    /* Copy target into PathBuffer twice (SubstituteName and PrintName) */
    memcpy(symlink.PathBuffer, target.c_str(), targetBytes);
    memcpy(reinterpret_cast<char *>(symlink.PathBuffer) + targetBytes, target.c_str(), targetBytes);

    DWORD bytesReturned;
    if (!DeviceIoControl(
            handle,
            FSCTL_SET_REPARSE_POINT,
            buf.data(),
            static_cast<DWORD>(bufSize),
            nullptr,
            0,
            &bytesReturned,
            nullptr))
        throw WinError("setting reparse point for symlink");
}

/**
 * Check if a handle refers to a reparse point (e.g., symlink).
 *
 * @param handle Open file/directory handle
 * @return true if the handle refers to a reparse point
 */
bool isReparsePoint(HANDLE handle)
{
    FILE_BASIC_INFO basicInfo;
    if (!GetFileInformationByHandleEx(handle, FileBasicInfo, &basicInfo, sizeof(basicInfo)))
        throw WinError("GetFileInformationByHandleEx");

    return (basicInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

/**
 * Convert Unix time_t to Windows FILETIME.
 *
 * FILETIME epoch is January 1, 1601 00:00:00 UTC.
 * Unix epoch is January 1, 1970 00:00:00 UTC.
 * Difference is 11644473600 seconds.
 * FILETIME is in 100-nanosecond intervals.
 */
FILETIME timeToFileTime(time_t t)
{
    constexpr uint64_t EPOCH_DIFF = 11644473600ULL;
    uint64_t intervals = (static_cast<uint64_t>(t) + EPOCH_DIFF) * 10000000ULL;
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(intervals);
    ft.dwHighDateTime = static_cast<DWORD>(intervals >> 32);
    return ft;
}

} // anonymous namespace

} // namespace windows

AutoCloseFD reopenDirectory(Descriptor dirFd)
{
    using namespace nix::windows;

    AutoCloseFD fd{ReOpenFile(
        dirFd,
        FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_FLAG_BACKUP_SEMANTICS)};
    if (!fd)
        throw WinError("reopening directory %s", PathFmt(descriptorToPath(dirFd)));
    return fd;
}

OsString readLinkAt(Descriptor dirFd, const OsCanonPath & path)
{
    using namespace nix::windows;

    AutoCloseFD linkHandle(openSymlinkAt(dirFd, path));
    return readSymlinkTarget(linkHandle.get());
}

void createFileSymlinkAt(Descriptor dirFd, const OsCanonPath & path, const OsString & target)
{
    using namespace nix::windows;

    assert(!path.empty());

    /* Create the file that will become the symlink */
    auto handle = ntOpenAt(
        dirFd, path.c_str(), GENERIC_WRITE | DELETE, FILE_NON_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT, FILE_CREATE);

    writeSymlinkTarget(handle.get(), target);
}

void createDirectorySymlinkAt(Descriptor dirFd, const OsCanonPath & path, const OsString & target)
{
    using namespace nix::windows;

    assert(!path.empty());

    /* Create the directory that will become the symlink */
    auto handle = ntOpenAt(
        dirFd, path.c_str(), GENERIC_WRITE | DELETE, FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT, FILE_CREATE);

    writeSymlinkTarget(handle.get(), target);
}

void createUnknownSymlinkAt(Descriptor dirFd, const OsCanonPath & path, const OsString & target)
{
    using namespace nix::windows;

    assert(!path.empty());

    std::filesystem::path targetPath(target);
    bool isDirectory = false;

    if (targetPath.is_absolute()) {
        /* For absolute targets, use std::filesystem::status directly */
        std::error_code ec;
        auto status = std::filesystem::status(targetPath, ec);
        isDirectory = !ec && std::filesystem::is_directory(status);
    } else {
        /* For relative targets, the target is relative to the symlink's parent directory.
           Open the parent directory first, then try to open the target relative to it. */
        Descriptor parentFd = dirFd;
        AutoCloseFD parentFdOwned;

        auto parentPath = path.path().parent_path();
        if (!parentPath.empty()) {
            /* Open the parent directory of the symlink */
            parentFdOwned = ntOpenAt(dirFd, parentPath.c_str(), FILE_TRAVERSE | SYNCHRONIZE, FILE_DIRECTORY_FILE);
            parentFd = parentFdOwned.get();
        }

        /* Try to open the target as a directory and verify with fstat. */
        auto result = maybeNtOpenAt(
            parentFd, targetPath.make_preferred().native(), FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_DIRECTORY_FILE);
        if (result) {
            auto st = fstat(result.value().get());
            isDirectory = S_ISDIR(st.st_mode);
        }
    }

    if (isDirectory)
        createDirectorySymlinkAt(dirFd, path, target);
    else
        createFileSymlinkAt(dirFd, path, target);
}

static Result<AutoCloseFD, NativeError>
openOrCreateDirectoryAt(Descriptor dirFd, const OsCanonPath & path, ULONG createDisposition)
{
    using namespace nix::windows;

    assert(!path.empty());

    // Use FILE_OPEN_REPARSE_POINT to avoid following symlinks
    // FILE_READ_ATTRIBUTES so that the result can be fstat-ed
    // FILE_CREATE creates and opens it atomically
    auto result = maybeNtOpenAt(
        dirFd,
        path.c_str(),
        FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT,
        createDisposition);
    if (result)
        return outcome::success(std::move(result.value()));

    return outcome::failure(RtlNtStatusToDosError(result.error()));
}

Result<AutoCloseFD, NativeError> openDirectoryAt(Descriptor dirFd, const OsCanonPath & path)
{
    return openOrCreateDirectoryAt(dirFd, path, FILE_OPEN);
}

Result<AutoCloseFD, NativeError> createDirectoryAt(Descriptor dirFd, const OsCanonPath & path)
{
    return openOrCreateDirectoryAt(dirFd, path, FILE_CREATE);
}

PosixStat fstat(Descriptor fd)
{
    using namespace nix::windows;

    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(fd, &info))
        throw WinError("getting file information for %s", PathFmt(descriptorToPath(fd)));

    PosixStat st;
    statFromFileInfo(
        st,
        info.dwFileAttributes,
        info.ftCreationTime,
        info.ftLastAccessTime,
        info.ftLastWriteTime,
        info.nFileSizeHigh,
        info.nFileSizeLow,
        info.nNumberOfLinks);

    return st;
}

PosixStat fstatat(Descriptor dirFd, const OsCanonPath & path)
{
    using namespace nix::windows;

    assert(!path.empty());

    /* Open the file without following symlinks */
    auto handle = ntOpenAt(dirFd, path.c_str(), FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_OPEN_REPARSE_POINT);

    return fstat(handle.get());
}

void setWriteTime(Descriptor dirFd, const OsCanonPath & path, time_t accessedTime, time_t modificationTime)
{
    using namespace nix::windows;

    assert(!path.empty());

    /* Open the file with write attributes permission, not following symlinks */
    AutoCloseFD handle(ntOpenAt(
        dirFd, path.c_str(), FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, FILE_OPEN_REPARSE_POINT /* Don't follow symlinks */
        ));

    FILETIME atime = timeToFileTime(accessedTime);
    FILETIME mtime = timeToFileTime(modificationTime);

    if (!SetFileTime(handle.get(), nullptr /* creation time */, &atime, &mtime))
        throw WinError("changing modification time of %s", PathFmt(path.path()));
}

std::optional<PosixStat> maybeFstatat(Descriptor dirFd, const OsCanonPath & path)
{
    using namespace nix::windows;

    assert(!path.empty());

    auto result = maybeNtOpenAt(dirFd, path.c_str(), FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_OPEN_REPARSE_POINT);
    if (result)
        return fstat(result.value().get());

    auto lastError = RtlNtStatusToDosError(result.error());
    if (lastError == ERROR_FILE_NOT_FOUND || lastError == ERROR_PATH_NOT_FOUND)
        return std::nullopt;
    throw WinError(lastError, "getting status of %s", PathFmt(descriptorToPath(dirFd) / path.path()));
}

Result<AutoCloseFD, NativeError> openFileEnsureBeneathNoSymlinks(
    Descriptor dirFd,
    const OsCanonPath & path,
    ACCESS_MASK desiredAccess,
    ULONG createOptions,
    ULONG createDisposition,
    /* FIXME: Actually call this callback. */
    [[maybe_unused]] std::function<void(AutoCloseFD dirFd, OsCanonPath relPath)> dirFdCallback)
{
    using namespace nix::windows;

    assert(!path.empty());

    AutoCloseFD parentFd;
    auto nrComponents = std::ranges::distance(path);
    assert(nrComponents >= 1);
    auto components = std::views::take(path, nrComponents - 1); /* Everything but last component */
    auto getParentFd = [&]() { return parentFd ? parentFd.get() : dirFd; };

    /* Helper to construct OsCanonPath from components up to (and including) the given iterator */
    auto pathUpTo = [&](auto it) {
        return std::ranges::fold_left(
            components.begin(), it, OsCanonPath{}, [](OsCanonPath acc, const OsFilename & comp) { return acc / comp; });
    };

    /* Helper to check if a component is a symlink and throw SymlinkNotAllowed if so */
    auto throwIfSymlink = [&](std::wstring_view component, const OsCanonPath & pathForError) {
        try {
            auto testHandle =
                ntOpenAt(getParentFd(), component, FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_OPEN_REPARSE_POINT);
            if (isReparsePoint(testHandle.get()))
                throw SymlinkNotAllowed(pathForError);
        } catch (SymlinkNotAllowed &) {
            throw;
        } catch (...) {
            /* If we can't determine, ignore and let caller handle original error */
        }
    };

    /* Iterate through each path component to ensure no symlinks in intermediate directories.
     * This prevents TOCTOU issues by opening each component relative to the parent. */
    for (auto it = components.begin(); it != components.end(); ++it) {
        const auto & wcomponent = (*it).path().native();

        /* Open directory without following symlinks */
        auto result = maybeNtOpenAt(
            getParentFd(),
            wcomponent,
            FILE_TRAVERSE | SYNCHRONIZE,                  // Just need traversal rights
            FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT // Open directory, don't follow symlinks
        );
        if (!result) {
            auto lastError = RtlNtStatusToDosError(result.error());
            /* Check if this is because it's a symlink - these errors can indicate
               we tried to traverse through a symlink or open a symlink as a directory */
            if (lastError == ERROR_CANT_ACCESS_FILE || lastError == ERROR_ACCESS_DENIED
                || lastError == ERROR_INVALID_NAME || lastError == ERROR_DIRECTORY) {
                throwIfSymlink(wcomponent, pathUpTo(std::next(it)));
            }
            /* `ERROR_DIRECTORY` means the component is not a directory (e.g.,
               it's a file), so the caller sees that the path doesn't exist. */
            return outcome::failure(lastError);
        }

        /* Check if what we opened is actually a symlink */
        if (isReparsePoint(result.value().get())) {
            throw SymlinkNotAllowed(pathUpTo(std::next(it)));
        }

        parentFd = std::move(result.value());
    }

    /* Now open the final component with requested flags */
    /* The iterator must outlive the reference, since the standard allows it to
       own the component. */
    auto finalIt = std::ranges::prev(path.end());
    const auto & finalComponent = (*finalIt).path().native();

    auto finalResult = maybeNtOpenAt(
        getParentFd(),
        finalComponent,
        desiredAccess,
        createOptions | FILE_OPEN_REPARSE_POINT, // Don't follow symlinks on final component either
        createDisposition);
    if (!finalResult) {
        auto lastError = RtlNtStatusToDosError(finalResult.error());
        /* Check if final component is a symlink when we requested to not follow it. */
        if (lastError == ERROR_CANT_ACCESS_FILE || lastError == ERROR_INVALID_NAME) {
            throwIfSymlink(finalComponent, path);
        }
        /* `ERROR_DIRECTORY` can also mean the final component is a
           *file* symlink being opened with `FILE_DIRECTORY_FILE`: the
           reparse point itself is then a file, whatever its target is.
           We create those ourselves, e.g. `createUnknownSymlinkAt` does
           when it can't tell that the target is a directory. So check for
           that too.

           This used to be done under Wine only, suspecting a Wine bug, but
           Wine attaches a reparse point to a file or directory host object
           just as NTFS does, so real Windows behaves the same. */
        if (lastError == ERROR_DIRECTORY) {
            throwIfSymlink(finalComponent, path);
        }
        return outcome::failure(lastError);
    }

    /* Final check: did we accidentally open a symlink? */
    if (isReparsePoint(finalResult.value().get()))
        throw SymlinkNotAllowed(path);

    return outcome::success(std::move(finalResult.value()));
}

/**
 * Delete @p name in the directory @p parentFd, like
 * `deletePathAt`. @p path is only used for messages.
 */
static void
deletePathIn(Descriptor parentFd, const OsFilename & name, const std::filesystem::path & path, uint64_t & bytesFreed)
{
    using namespace nix::windows;

    checkInterrupt();

    /* `DELETE` to delete it, and `FILE_WRITE_ATTRIBUTES` to make it
       deletable if it is read-only. Not following symlinks, so that
       they are deleted themselves. */
    auto result = maybeNtOpenAt(
        parentFd,
        name.path().native(),
        DELETE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE,
        FILE_OPEN_REPARSE_POINT);
    if (!result) {
        auto lastError = RtlNtStatusToDosError(result.error());
        if (lastError == ERROR_FILE_NOT_FOUND || lastError == ERROR_PATH_NOT_FOUND)
            return;
        throw WinError(lastError, "opening %s for deletion", PathFmt(path));
    }
    auto handle = std::move(result.value());

    FILE_BASIC_INFO basicInfo;
    if (!GetFileInformationByHandleEx(handle.get(), FileBasicInfo, &basicInfo, sizeof(basicInfo)))
        throw WinError("getting attributes of %s", PathFmt(path));

    if ((basicInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        && !(basicInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        for (const auto & entry : DirectoryIterator{reopenDirectory(handle.get())})
            deletePathIn(handle.get(), entry.name, path / entry.name.path(), bytesFreed);
    } else {
        /* Like on Unix, assume that a file with two links was
           optimised, so that deleting it frees space too. */
        auto st = fstat(handle.get());
        if (st.st_nlink <= 2)
            bytesFreed += st.st_size;
    }

    /* Deleting fails on read-only files, so clear that first. Zero
       means "unchanged" to `SetFileInformationByHandle`, hence
       `FILE_ATTRIBUTE_NORMAL` when no other attribute is left. */
    if (basicInfo.FileAttributes & FILE_ATTRIBUTE_READONLY) {
        basicInfo.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
        if (!basicInfo.FileAttributes)
            basicInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
        if (!SetFileInformationByHandle(handle.get(), FileBasicInfo, &basicInfo, sizeof(basicInfo)))
            throw WinError("making %s writable for deletion", PathFmt(path));
    }

    /* Deleted once `handle` is closed. */
    FILE_DISPOSITION_INFO dispositionInfo{.DeleteFile = TRUE};
    if (!SetFileInformationByHandle(handle.get(), FileDispositionInfo, &dispositionInfo, sizeof(dispositionInfo)))
        throw WinError("deleting %s", PathFmt(path));
}

void deletePathAt(Descriptor dirFd, const OsCanonPath & path, uint64_t & bytesFreed)
{
    using namespace nix::windows;

    assert(!path.empty());

    bytesFreed = 0;

    Descriptor parentFd = dirFd;
    AutoCloseFD parentFdOwned;
    if (auto parent = path.parent(); !parent->empty()) {
        auto parentFdOrError = openFileEnsureBeneathNoSymlinks(
            dirFd,
            *parent,
            FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            FILE_DIRECTORY_FILE);
        if (!parentFdOrError) {
            auto lastError = parentFdOrError.error();
            if (lastError == ERROR_FILE_NOT_FOUND || lastError == ERROR_PATH_NOT_FOUND)
                return;
            throw WinError(lastError, "opening directory %s", PathFmt(descriptorToPath(dirFd) / parent->path()));
        }
        parentFdOwned = std::move(parentFdOrError).value();
        parentFd = parentFdOwned.get();
    }

    auto lastIt = std::ranges::prev(path.end());
    deletePathIn(parentFd, *lastIt, descriptorToPath(dirFd) / path.path(), bytesFreed);
}

void deletePathAt(Descriptor dirFd, const OsCanonPath & path)
{
    uint64_t dummy;
    deletePathAt(dirFd, path, dummy);
}

/**
 * Open the parent directory of @p path beneath @p dirFd, without
 * following symlinks, for `*At` functions that operate on the final
 * component of @p path.
 *
 * @return The parent directory, owned if it had to be opened, and the
 * final component of @p path.
 */
static std::tuple<AutoCloseFD, Descriptor, OsFilename> openParentBeneath(Descriptor dirFd, const OsCanonPath & path)
{
    using namespace nix::windows;

    assert(!path.empty());
    auto lastIt = std::ranges::prev(path.end());
    OsFilename name = *lastIt;
    auto parent = path.parent();
    if (parent->empty())
        return {AutoCloseFD{}, dirFd, std::move(name)};
    auto parentFd = valueOr(
        openFileEnsureBeneathNoSymlinks(
            dirFd,
            *parent,
            FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            FILE_DIRECTORY_FILE),
        NIX_THROW_NATIVE_SYS_ERROR_HANDLER("opening directory %s", PathFmt(descriptorToPath(dirFd) / parent->path())));
    auto fd = parentFd.get();
    return {std::move(parentFd), fd, std::move(name)};
}

void movePathAt(Descriptor srcDirFd, const OsCanonPath & src, Descriptor dstDirFd, const OsCanonPath & dst)
{
    using namespace nix::windows;

    auto [srcParentOwned, srcParent, srcName] = openParentBeneath(srcDirFd, src);
    auto [dstParentOwned, dstParent, dstName] = openParentBeneath(dstDirFd, dst);

    /* Not following symlinks, so that they are moved themselves. */
    auto handle = ntOpenAt(srcParent, srcName.path().native(), DELETE | SYNCHRONIZE, FILE_OPEN_REPARSE_POINT);

    /* `FILE_RENAME_INFO` ends in the variable-length new name, which is
       relative to `RootDirectory`. */
    const auto & newName = dstName.path().native();
    auto nameBytes = newName.size() * sizeof(wchar_t);
    std::vector<std::byte> buf(std::max(sizeof(FILE_RENAME_INFO), offsetof(FILE_RENAME_INFO, FileName) + nameBytes));
    auto * info = reinterpret_cast<FILE_RENAME_INFO *>(buf.data());
    info->ReplaceIfExists = TRUE;
    info->RootDirectory = dstParent;
    info->FileNameLength = static_cast<DWORD>(nameBytes);
    std::memcpy(info->FileName, newName.data(), nameBytes);

    if (!SetFileInformationByHandle(handle.get(), FileRenameInfo, info, static_cast<DWORD>(buf.size())))
        throw WinError(
            "renaming %1% to %2%",
            PathFmt(descriptorToPath(srcDirFd) / src.path()),
            PathFmt(descriptorToPath(dstDirFd) / dst.path()));
}

} // namespace nix
