#include "util.hh"
#include "local-store.hh"
#include "globals.hh"

#include <cstdlib>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <regex>

#ifdef __MINGW32__
#define random() rand()
#include <iostream>
#endif

namespace nix {

#ifndef __MINGW32__
static void makeWritable(const Path & path)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw PosixError(format("getting attributes of path '%1%'") % path);
    if (chmod(path.c_str(), st.st_mode | S_IWUSR) == -1)
        throw PosixError(format("changing writability of '%1%'") % path);
}


struct MakeReadOnly
{
    Path path;
    MakeReadOnly(const Path & path) : path(path) { }
    ~MakeReadOnly()
    {
        try {
            /* This will make the path read-only. */
            if (path != "") canonicaliseTimestampAndPermissions(path);
        } catch (...) {
            ignoreException();
        }
    }
};
#endif

LocalStore::InodeHash LocalStore::loadInodeHash()
{
    debug("loading hash inodes in memory");
    InodeHash inodeHash;
#ifndef __MINGW32__
    AutoCloseDir dir(opendir(linksDir.c_str()));
    if (!dir) throw PosixError(format("opening directory '%1%'") % linksDir);

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir.get())) { /* sic */
        checkInterrupt();
        // We don't care if we hit non-hash files, anything goes
        inodeHash.insert(dirent->d_ino);
    }
    if (errno) throw PosixError(format("reading directory '%1%'") % linksDir);
#else
    WIN32_FIND_DATAW wfd;
    std::wstring wlinksDir = pathW(linksDir);
    HANDLE hFind = FindFirstFileExW((wlinksDir + L"\\*").c_str(), FindExInfoBasic, &wfd, FindExSearchNameMatch, NULL, 0);
    if (hFind == INVALID_HANDLE_VALUE) {
        throw WinError("FindFirstFileExW when LocalStore::loadInodeHash()");
    } else {
        do {
            bool isDot    = (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && wfd.cFileName[0]==L'.' && wfd.cFileName[1]==L'\0';
            bool isDotDot = (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && wfd.cFileName[0]==L'.' && wfd.cFileName[1]==L'.' && wfd.cFileName[2]==L'\0';
            if (isDot || isDotDot)
                continue;

            checkInterrupt();

            BY_HANDLE_FILE_INFORMATION bhfi;
            std::wstring wpath = wlinksDir + L'\\' + wfd.cFileName;
            HANDLE hFile = CreateFileW(wpath.c_str(), 0, FILE_SHARE_READ, 0, OPEN_EXISTING,
                                       FILE_FLAG_POSIX_SEMANTICS |
                                       ((wfd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ? FILE_FLAG_OPEN_REPARSE_POINT : 0) |
                                       ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY    ) != 0 ? FILE_FLAG_BACKUP_SEMANTICS   : 0),
                                       0);
            if (hFile == INVALID_HANDLE_VALUE)
                throw WinError("CreateFileW when LocalStore::loadInodeHash() '%1%'", to_bytes(wpath));
            if (!GetFileInformationByHandle(hFile, &bhfi))
                throw WinError("GetFileInformationByHandle when LocalStore::loadInodeHash() '%1%'", to_bytes(wpath));
            CloseHandle(hFile);

            inodeHash.insert((uint64_t(bhfi.nFileIndexHigh)<<32) +  bhfi.nFileIndexLow);
        } while(FindNextFileW(hFind, &wfd));
        WinError winError("FindNextFileW when LocalStore::loadInodeHash()");
        if (winError.lastError != ERROR_NO_MORE_FILES)
            throw winError;
        FindClose(hFind);
    }
#endif
    printMsg(lvlTalkative, format("loaded %1% hash inodes") % inodeHash.size());

    return inodeHash;
}


Strings LocalStore::readDirectoryIgnoringInodes(const Path & path, const InodeHash & inodeHash)
{
    Strings names;
#ifndef __MINGW32__
    AutoCloseDir dir(opendir(path.c_str()));
    if (!dir) throw PosixError(format("opening directory '%1%'") % path);

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir.get())) { /* sic */
        checkInterrupt();

        if (inodeHash.count(dirent->d_ino)) {
            debug(format("'%1%' is already linked") % dirent->d_name);
            continue;
        }

        string name = dirent->d_name;
        if (name == "." || name == "..") continue;
        names.push_back(name);
    }
    if (errno) throw PosixError(format("reading directory '%1%'") % path);
#else
    WIN32_FIND_DATAW wfd;
    std::wstring wpath = pathW(path);
    HANDLE hFind = FindFirstFileExW((wpath + L"\\*").c_str(), FindExInfoBasic, &wfd, FindExSearchNameMatch, NULL, 0);
    if (hFind == INVALID_HANDLE_VALUE) {
        throw WinError("FindFirstFileExW when LocalStore::readDirectoryIgnoringInodes()");
    } else {
        do {
            bool isDot    = (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && wfd.cFileName[0]==L'.' && wfd.cFileName[1]==L'\0';
            bool isDotDot = (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && wfd.cFileName[0]==L'.' && wfd.cFileName[1]==L'.' && wfd.cFileName[2]==L'\0';
            if (isDot || isDotDot)
                continue;

            checkInterrupt();

            BY_HANDLE_FILE_INFORMATION bhfi;
            std::wstring wsubpath = wpath + L'\\' + wfd.cFileName;
            HANDLE hFile = CreateFileW(wsubpath.c_str(), 0, FILE_SHARE_READ, 0, OPEN_EXISTING,
                                       FILE_FLAG_POSIX_SEMANTICS |
                                       ((wfd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ? FILE_FLAG_OPEN_REPARSE_POINT : 0) |
                                       ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY    ) != 0 ? FILE_FLAG_BACKUP_SEMANTICS   : 0),
                                       0);
            if (hFile == INVALID_HANDLE_VALUE)
                throw WinError("CreateFileW when LocalStore::readDirectoryIgnoringInodes() '%1%'", to_bytes(wsubpath));
            if (!GetFileInformationByHandle(hFile, &bhfi))
                throw WinError("GetFileInformationByHandle when LocalStore::readDirectoryIgnoringInodes() '%1%'", to_bytes(wsubpath));
            CloseHandle(hFile);
            assert(((uint64_t(bhfi.nFileSizeHigh) << 32) + bhfi.nFileSizeLow) == ((uint64_t(wfd.nFileSizeHigh) << 32) + wfd.nFileSizeLow));

            if (inodeHash.count((uint64_t(bhfi.nFileIndexHigh)<<32) +  bhfi.nFileIndexLow)) {
                debug(format("'%1%' is already linked") % to_bytes(wsubpath));
                continue;
            }

            string name = to_bytes(wfd.cFileName);
            names.push_back(name);
        } while(FindNextFileW(hFind, &wfd));
        WinError winError("FindNextFileW when LocalStore::readDirectoryIgnoringInodes()");
        if (winError.lastError != ERROR_NO_MORE_FILES)
            throw winError;
        FindClose(hFind);
    }
#endif
    return names;
}


void LocalStore::optimisePath_(Activity * act, OptimiseStats & stats,
    const Path & path, InodeHash & inodeHash)
{
//std::cerr << "optimisePath_(" << path << ")" << std::endl;
    checkInterrupt();


#ifndef __MINGW32__
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw PosixError(format("getting attributes of path '%1%'") % path);

#if __APPLE__
    /* HFS/macOS has some undocumented security feature disabling hardlinking for
       special files within .app dirs. *.app/Contents/PkgInfo and
       *.app/Contents/Resources/\*.lproj seem to be the only paths affected. See
       https://github.com/NixOS/nix/issues/1443 for more discussion. */

    if (std::regex_search(path, std::regex("\\.app/Contents/.+$")))
    {
        debug(format("'%1%' is not allowed to be linked in macOS") % path);
        return;
    }
#endif
#else
    std::wstring wpath = pathW(path);

    WIN32_FILE_ATTRIBUTE_DATA wfad;
    if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &wfad))
        throw WinError("GetFileAttributesExW when LocalStore::optimisePath_ '%1%'", path);
#endif


#ifndef __MINGW32__
    if (S_ISDIR(st.st_mode)) {
#else
    if ((wfad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && (wfad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
#endif
        Strings names = readDirectoryIgnoringInodes(path, inodeHash);
        for (auto & i : names)
            optimisePath_(act, stats, path + "/" + i, inodeHash);
        return;
    }

    /* We can hard link regular files and maybe symlinks. */
#ifndef __MINGW32__
    if (!S_ISREG(st.st_mode)
#if CAN_LINK_SYMLINK
        && !S_ISLNK(st.st_mode)
#endif
        ) return;
#else
    if ((wfad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return;
    }
#endif



    /* Sometimes SNAFUs can cause files in the Nix store to be
       modified, in particular when running programs as root under
       NixOS (example: $fontconfig/var/cache being modified).  Skip
       those files.  FIXME: check the modification time. */
#ifndef __MINGW32__
    if (S_ISREG(st.st_mode) && (st.st_mode & S_IWUSR)) {
#else
    if ((wfad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0) {
#endif
        printError(format("skipping suspicious writable file '%1%'") % path);
        return;
    }

    /* This can still happen on top-level files. */
#ifndef __MINGW32__
    if (st.st_nlink > 1 && inodeHash.count(st.st_ino)) {
        debug(format("'%1%' is already linked, with %2% other file(s)") % path % (st.st_nlink - 2));
#else
    BY_HANDLE_FILE_INFORMATION bhfi;
    HANDLE hFile = CreateFileW(wpath.c_str(), 0, FILE_SHARE_READ, 0, OPEN_EXISTING,
                               FILE_FLAG_POSIX_SEMANTICS /*|
                               ((dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ? FILE_FLAG_OPEN_REPARSE_POINT : 0) |
                               ((dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY    ) != 0 ? FILE_FLAG_BACKUP_SEMANTICS   : 0)*/,
                               0);
    if (hFile == INVALID_HANDLE_VALUE)
        throw WinError("CreateFileW when LocalStore::optimisePath_ '%1%'", path);
    if (!GetFileInformationByHandle(hFile, &bhfi))
        throw WinError("GetFileInformationByHandle when LocalStore::optimisePath_ '%1%'", path);
    CloseHandle(hFile);
    assert(((uint64_t(bhfi.nFileSizeHigh) << 32) + bhfi.nFileSizeLow) == ((uint64_t(wfad.nFileSizeHigh) << 32) + wfad.nFileSizeLow));

    const uint64_t ino = (uint64_t(bhfi.nFileIndexHigh)<<32) +  bhfi.nFileIndexLow;
    if (bhfi.nNumberOfLinks > 1 && inodeHash.count(ino)) {
        debug(format("'%1%' is already linked, with %2% other file(s)") % path % (bhfi.nNumberOfLinks - 2));
#endif
        return;
    }

    /* Hash the file.  Note that hashPath() returns the hash over the
       NAR serialisation, which includes the execute bit on the file.
       Thus, executable and non-executable files with the same
       contents *won't* be linked (which is good because otherwise the
       permissions would be screwed up).

       Also note that if `path' is a symlink, then we're hashing the
       contents of the symlink (i.e. the result of readlink()), not
       the contents of the target (which may not even exist). */
    Hash hash = hashPath(htSHA256, path).first;
    debug(format("'%1%' has hash '%2%'") % path % hash.to_string());

    /* Check if this is a known hash. */
    Path linkPath = linksDir + "/" + hash.to_string(Base32, false);

 retry:
    if (!pathExists(linkPath)) {
#ifndef __MINGW32__
        /* Nope, create a hard link in the links directory. */
        if (link(path.c_str(), linkPath.c_str()) == 0) {
            inodeHash.insert(st.st_ino);
            return;
        }

        switch (errno) {
        case EEXIST:
            /* Fall through if another process created ‘linkPath’ before
               we did. */
            break;

        case ENOSPC:
            /* On ext4, that probably means the directory index is
               full.  When that happens, it's fine to ignore it: we
               just effectively disable deduplication of this
               file.  */
            printInfo("cannot link '%s' to '%s': %s", linkPath, path, strerror(errno));
            return;

        default:
            throw PosixError("cannot link '%1%' to '%2%'", linkPath, path);
        }
#else
        if (CreateHardLinkW(pathW(linkPath).c_str(), wpath.c_str(), NULL)) {
            return;
        }
        WinError winError("CreateHardLinkW-1 '%1%' '%2%'", path, linkPath);
        throw winError;
        // todo: fall through if already exist
        // todo: handle maximum number of links error
#endif
    }

#ifndef __MINGW32__
    /* Yes!  We've seen a file with the same contents.  Replace the
       current file with a hard link to that file. */
    struct stat stLink;
    if (lstat(linkPath.c_str(), &stLink))
        throw PosixError(format("getting attributes of path '%1%'") % linkPath);
#else
    BY_HANDLE_FILE_INFORMATION bhfi2;
    HANDLE hFile2 = CreateFileW(pathW(linkPath).c_str(), 0, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_FLAG_POSIX_SEMANTICS, 0);
    if (hFile2 == INVALID_HANDLE_VALUE)
        throw WinError("CreateFileW when LocalStore::optimisePath_ '%1%'", linkPath);
    if (!GetFileInformationByHandle(hFile2, &bhfi2))
        throw WinError("GetFileInformationByHandle when LocalStore::optimisePath_ '%1%'", linkPath);
    CloseHandle(hFile2);

    const uint64_t ino2 = (uint64_t(bhfi2.nFileIndexHigh)<<32) +  bhfi2.nFileIndexLow;
#endif

#ifndef __MINGW32__
    if (st.st_ino == stLink.st_ino) {
#else
    if (ino == ino2) {
#endif
        debug(format("'%1%' is already linked to '%2%'") % path % linkPath);
        return;
    }


#ifndef __MINGW32__
    if (st.st_size != stLink.st_size) {
#else
    if (((uint64_t(bhfi.nFileSizeHigh) << 32) + bhfi.nFileSizeLow) != ((uint64_t(bhfi2.nFileSizeHigh) << 32) + bhfi2.nFileSizeLow)) {
#endif
        printError(format("removing corrupted link '%1%'") % linkPath);
#ifndef __MINGW32__
        unlink(linkPath.c_str());
#else
        DeleteFileW(pathW(linkPath).c_str());
#endif
        goto retry;
    }

    printMsg(lvlTalkative, format("linking '%1%' to '%2%'") % path % linkPath);

#ifndef __MINGW32__
    /* Make the containing directory writable, but only if it's not
       the store itself (we don't want or need to mess with its
       permissions). */
    bool mustToggle = dirOf(path) != realStoreDir;
    if (mustToggle) makeWritable(dirOf(path));

    /* When we're done, make the directory read-only again and reset
       its timestamp back to 0. */
    MakeReadOnly makeReadOnly(mustToggle ? dirOf(path) : "");
#endif

    Path tempLink = (format("%1%/.tmp-link-%2%-%3%")
        % realStoreDir % getpid() % random()).str();
#ifndef __MINGW32__
    if (link(linkPath.c_str(), tempLink.c_str()) == -1) {
        if (errno == EMLINK) {
            /* Too many links to the same file (>= 32000 on most file
               systems).  This is likely to happen with empty files.
               Just shrug and ignore. */
            if (st.st_size)
                printInfo(format("'%1%' has maximum number of links") % linkPath);
            return;
        }
        throw PosixError("cannot link '%1%' to '%2%'", tempLink, linkPath);
    }
#else
    if (!CreateHardLinkW(pathW(tempLink).c_str(), pathW(linkPath).c_str(), NULL)) {
        throw WinError("CreateHardLinkW-2 '%1%' '%2%'", linkPath, tempLink);
    }
#endif

    /* Atomically replace the old file with the new hard link. */
#ifndef __MINGW32__
    if (rename(tempLink.c_str(), path.c_str()) == -1) {
        if (unlink(tempLink.c_str()) == -1)
            printError(format("unable to unlink '%1%'") % tempLink);
        if (errno == EMLINK) {
            /* Some filesystems generate too many links on the rename,
               rather than on the original link.  (Probably it
               temporarily increases the st_nlink field before
               decreasing it again.) */
            debug("'%s' has reached maximum number of links", linkPath);
            return;
        }
        throw PosixError(format("cannot rename '%1%' to '%2%'") % tempLink % path);
    }
#else
    if ((wfad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0)
        if (!SetFileAttributesW(wpath.c_str(), wfad.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY))
            throw WinError("SetFileAttributes '%1%'", path);

    if (!MoveFileExW(pathW(tempLink).c_str(), wpath.c_str(), MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        throw WinError("MoveFileExW '%1%' '%2%'", tempLink, path);
    }

#ifdef _NDEBUG
    WIN32_FILE_ATTRIBUTE_DATA wfad2;
    if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &wfad2))
        throw WinError("GetFileAttributesExW-2 when LocalStore::optimisePath_ '%1%'", path);
    assert((wfad2.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0);
#endif
#endif

    stats.filesLinked++;
#ifndef __MINGW32__
    stats.bytesFreed += st.st_size;
    stats.blocksFreed += st.st_blocks;

    if (act)
        act->result(resFileLinked, st.st_size, st.st_blocks);
#else
    stats.bytesFreed += (uint64_t(bhfi.nFileSizeHigh) << 32) + bhfi.nFileSizeLow;
    if (act)
        act->result(resFileLinked, (uint64_t(bhfi.nFileSizeHigh) << 32) + bhfi.nFileSizeLow);
#endif
}


void LocalStore::optimiseStore(OptimiseStats & stats)
{
    Activity act(*logger, actOptimiseStore);

    PathSet paths = queryAllValidPaths();
    InodeHash inodeHash = loadInodeHash();

    act.progress(0, paths.size());

    uint64_t done = 0;

    for (auto & i : paths) {
        addTempRoot(i);
        if (!isValidPath(i)) continue; /* path was GC'ed, probably */
        {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("optimising path '%s'", i));
            optimisePath_(&act, stats, realStoreDir + "/" + baseNameOf(i), inodeHash);
        }
        done++;
        act.progress(done, paths.size());
    }
}

static string showBytes(unsigned long long bytes)
{
    return (format("%.2f MiB") % (bytes / (1024.0 * 1024.0))).str();
}

void LocalStore::optimiseStore()
{
    OptimiseStats stats;

    optimiseStore(stats);

    printInfo(
        format("%1% freed by hard-linking %2% files")
        % showBytes(stats.bytesFreed)
        % stats.filesLinked);
}

void LocalStore::optimisePath(const Path & path)
{
    OptimiseStats stats;
    InodeHash inodeHash;

    if (settings.autoOptimiseStore) optimisePath_(nullptr, stats, path, inodeHash);
}


}
