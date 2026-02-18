#include "nix/store/posix-fs-canonicalise.hh"
#include "nix/store/build-result.hh"
#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"
#include "nix/util/util.hh"
#include "nix/store/store-api.hh"
#include "nix/store/globals.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/canon-path.hh"
#include "store-config-private.hh"

#ifndef _WIN32
#  include <fcntl.h>
#  include <dirent.h>
#  include <sys/stat.h>
#endif

#if NIX_SUPPORT_ACL
#  include <sys/xattr.h>
#endif

namespace nix {

const time_t mtimeStore = 1; /* 1 second into the epoch */

static void canonicaliseTimestampAndPermissions(Descriptor dirFd, const CanonPath & path, const struct stat & st)
{
    if (!S_ISLNK(st.st_mode)) {
        /* Mask out all type related bits. */
        mode_t mode = st.st_mode & ~S_IFMT;
        bool isDir = S_ISDIR(st.st_mode);
        if ((mode != 0444 || isDir) && mode != 0555) {
            mode = 0444 | (st.st_mode & S_IXUSR || isDir ? 0111 : 0);
#ifndef _WIN32
            unix::fchmodatTryNoFollow(dirFd, path, mode);
#else
            // TODO: implement fchmodatTryNoFollow for Windows
#endif
        }
    }

    if (st.st_mtime != mtimeStore) {
        setWriteTime(dirFd, path, st.st_atime, mtimeStore);
    }
}

void canonicaliseTimestampAndPermissions(const Path & path)
{
    auto parent = std::filesystem::path(path).parent_path();
    auto name = std::filesystem::path(path).filename();

    if (parent.empty())
        parent = ".";

    AutoCloseFD dirFd = openDirectory(parent);
    if (!dirFd)
        throw SysError("opening parent directory of '%s'", path);

    auto namePath = CanonPath(name.string());
    auto st = fstatat(dirFd.get(), namePath);

    canonicaliseTimestampAndPermissions(dirFd.get(), namePath, st);
}

static void canonicalisePathMetaData_(
    Descriptor dirFd, const CanonPath & path, CanonicalizePathMetadataOptions options, InodesSeen & inodesSeen)
{
    checkInterrupt();

    auto st = fstatat(dirFd, path);

#ifdef __APPLE__
    /* Remove flags, in particular UF_IMMUTABLE which would prevent
       the file from being garbage-collected. FIXME: Use
       setattrlist() to remove other attributes as well. */
    AutoCloseFD fd = openat(dirFd, path.rel_c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (!fd)
        throw SysError("opening '%s' to clear flags", path);
    if (fchflags(fd.get(), 0)) {
        if (errno != ENOTSUP)
            throw SysError("clearing flags of path '%1%'", path);
    }
#endif

    /* Really make sure that the path is of a supported type. */
    if (!(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)))
        throw Error("file '%1%' has an unsupported type", path);

#if NIX_SUPPORT_ACL
    /* Remove extended attributes / ACLs. */
    /* We need a file descriptor for xattr operations on Linux. */
    AutoCloseFD fd = openat(
        dirFd,
        path.rel_c_str(),
        O_RDONLY | O_NOFOLLOW | O_CLOEXEC
#  ifdef O_PATH
            | O_PATH
#  endif
    );
    if (!fd)
        throw SysError("opening '%s' to remove extended attributes", path);

    ssize_t eaSize = flistxattr(fd.get(), nullptr, 0);

    if (eaSize < 0) {
        if (errno != ENOTSUP && errno != ENODATA)
            throw SysError("querying extended attributes of '%s'", path);
    } else if (eaSize > 0) {
        std::vector<char> eaBuf(eaSize);

        if ((eaSize = flistxattr(fd.get(), eaBuf.data(), eaBuf.size())) < 0)
            throw SysError("querying extended attributes of '%s'", path);

        for (auto & eaName : tokenizeString<Strings>(std::string(eaBuf.data(), eaSize), std::string("\000", 1))) {
            if (options.ignoredAcls.count(eaName))
                continue;
            if (fremovexattr(fd.get(), eaName.c_str()) == -1)
                throw SysError("removing extended attribute '%s' from '%s'", eaName, path);
        }
    }
#endif

#ifndef _WIN32
    /* Fail if the file is not owned by the build user.  This prevents
       us from messing up the ownership/permissions of files
       hard-linked into the output (e.g. "ln /etc/shadow $out/foo").
       However, ignore files that we chown'ed ourselves previously to
       ensure that we don't fail on hard links within the same build
       (i.e. "touch $out/foo; ln $out/foo $out/bar"). */
    if (options.uidRange && (st.st_uid < options.uidRange->first || st.st_uid > options.uidRange->second)) {
        if (S_ISDIR(st.st_mode) || !inodesSeen.count(Inode(st.st_dev, st.st_ino)))
            throw BuildError(BuildResult::Failure::OutputRejected, "invalid ownership on file '%1%'", path);
        mode_t mode = st.st_mode & ~S_IFMT;
        assert(
            S_ISLNK(st.st_mode)
            || (st.st_uid == geteuid() && (mode == 0444 || mode == 0555) && st.st_mtime == mtimeStore));
        return;
    }
#endif

    inodesSeen.insert(Inode(st.st_dev, st.st_ino));

    canonicaliseTimestampAndPermissions(dirFd, path, st);

#ifndef _WIN32
    /* Change ownership to the current uid. */
    if (st.st_uid != geteuid()) {
        if (fchownat(dirFd, path.rel_c_str(), geteuid(), getegid(), AT_SYMLINK_NOFOLLOW) == -1)
            throw SysError("changing owner of '%1%' to %2%", path, geteuid());
    }
#endif

    if (S_ISDIR(st.st_mode)) {
        AutoCloseFD childDirFd = openFileEnsureBeneathNoSymlinks(
            dirFd,
            path,
#ifdef _WIN32
            FILE_LIST_DIRECTORY | SYNCHRONIZE,
            FILE_DIRECTORY_FILE
#else
            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
#endif
        );
        if (!childDirFd)
            throw SysError("opening directory '%s'", path);

        AutoCloseDir dir(fdopendir(dup(childDirFd.get())));
        if (!dir)
            throw SysError("opening directory '%s' for iteration", path);

        struct dirent * dirent;
        while (errno = 0, dirent = readdir(dir.get())) {
            checkInterrupt();
            std::string childName = dirent->d_name;
            if (childName == "." || childName == "..")
                continue;
            canonicalisePathMetaData_(childDirFd.get(), CanonPath(childName), options, inodesSeen);
        }
        if (errno)
            throw SysError("reading directory '%s'", path);
    }
}

void canonicalisePathMetaData(const Path & path, CanonicalizePathMetadataOptions options, InodesSeen & inodesSeen)
{
    auto parent = std::filesystem::path(path).parent_path();
    auto name = std::filesystem::path(path).filename();

    if (parent.empty())
        parent = ".";

    AutoCloseFD dirFd = openDirectory(parent);
    if (!dirFd)
        throw SysError("opening parent directory of '%s'", path);

    canonicalisePathMetaData_(dirFd.get(), CanonPath(name.string()), options, inodesSeen);
}

void canonicalisePathMetaData(const Path & path, CanonicalizePathMetadataOptions options)
{
    InodesSeen inodesSeen;
    canonicalisePathMetaData(path, options, inodesSeen);
}

} // namespace nix
