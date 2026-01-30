#include "nix/store/posix-fs-canonicalise.hh"
#include "nix/store/build-result.hh"
#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"
#include "nix/store/store-api.hh"
#include "nix/util/fs-sink.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/os-filename.hh"
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

#include <sys/stat.h>

namespace nix {

static constexpr time_t mtimeStore = 1; /* 1 second into the epoch */

static void canonicaliseTimestampAndPermissions(Descriptor dirFd, const OsFilename & path, const PosixStat & st)
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

void canonicaliseTimestampAndPermissions(Descriptor dirFd, const OsFilename & path)
{
    canonicaliseTimestampAndPermissions(dirFd, path, fstatat(dirFd, path));
}

#if NIX_SUPPORT_ACL

/**
 * The common part of `stripXAttrs()` and `stripXAttrsAt()`.
 *
 * @param list Like `listxattr` without its first argument.
 * @param remove Like `removexattr` without its first argument.
 * @param describe The path to show in error messages.
 */
static void stripXAttrsWith(auto list, auto remove, auto describe, const StringSet & ignoredAcls)
{
    ssize_t eaSize = list(nullptr, 0);

    if (eaSize < 0) {
        if (errno != ENOTSUP && errno != ENODATA)
            throw SysError([&]() { return HintFmt("querying extended attributes of %s", PathFmt(describe())); });
    } else if (eaSize > 0) {
        std::vector<char> eaBuf(eaSize);

        if ((eaSize = list(eaBuf.data(), eaBuf.size())) < 0)
            throw SysError([&]() { return HintFmt("querying extended attributes of %s", PathFmt(describe())); });

        using namespace std::string_view_literals;
        for (auto & eaName : tokenizeString<Strings>(std::string_view(eaBuf.data(), eaSize), "\0"sv)) {
            if (ignoredAcls.count(eaName))
                continue;
            if (remove(eaName.c_str()) == -1)
                throw SysError(
                    [&]() { return HintFmt("removing extended attribute '%s' from %s", eaName, PathFmt(describe())); });
        }
    }
}

/**
 * Remove all extended attributes / ACLs from @p fd, other than those
 * in @p ignoredAcls.
 *
 * @param fd Must not be an `O_PATH` descriptor; see `stripXAttrsAt()`
 * for when only one of those can be had.
 */
static void stripXAttrs(Descriptor fd, const StringSet & ignoredAcls)
{
    stripXAttrsWith(
        [&](char * list, size_t size) { return ::flistxattr(fd, list, size); },
        [&](const char * name) { return ::fremovexattr(fd, name); },
        [&]() { return descriptorToPath(fd); },
        ignoredAcls);
}

/**
 * Like `stripXAttrs()`, for the entry @p name in @p dirFd, not
 * following symlinks, whatever its type and permissions.
 */
static void stripXAttrsAt(Descriptor dirFd, const OsFilename & name, const StringSet & ignoredAcls)
{
    auto describe = [&]() { return descriptorToPath(dirFd) / name.path(); };

    /* `O_PATH`, so this works whatever the file's permissions, and for
       symlinks too. The xattr calls don't take `O_PATH` descriptors, so
       we go through the `/proc/self/fd` magic link instead, following
       it (which, being a magic link, gets us the entry itself even if
       it is a symlink).

       TODO: Linux 6.13 added `listxattrat`/`removexattrat` (and
       `getxattrat`/`setxattrat`), which can work on @p name relative to
       @p dirFd with `AT_SYMLINK_NOFOLLOW` directly, with no `O_PATH`
       descriptor or `/proc` needed:
       https://github.com/torvalds/linux/commit/6140be90ec70c39fa844741ca3cc807dd0866394
       glibc doesn't wrap them yet, so they would need `syscall()`, and
       we would still need this as a fallback (`ENOSYS`) for older
       kernels. */
    AutoCloseFD fd = ::openat(dirFd, name.c_str(), O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (!fd)
        throw SysError([&]() { return HintFmt("opening %s to remove extended attributes", PathFmt(describe())); });
    auto magicLink = "/proc/self/fd/" + std::to_string(fd.get());

    stripXAttrsWith(
        [&](char * list, size_t size) { return ::listxattr(magicLink.c_str(), list, size); },
        [&](const char * attrName) { return ::removexattr(magicLink.c_str(), attrName); },
        describe,
        ignoredAcls);
}

#endif

static void canonicalisePathMetaData_(
    Descriptor dirFd, const OsFilename & path, CanonicalizePathMetadataOptions options, InodesSeen & inodesSeen)
{
    checkInterrupt();

#ifdef __APPLE__
    /* Remove flags, in particular UF_IMMUTABLE which would prevent
       the file from being garbage-collected. FIXME: Use
       setattrlist() to remove other attributes as well. */
    /* `O_SYMLINK` rather than `O_NOFOLLOW`, so that symlinks are opened
       themselves instead of failing. */
    AutoCloseFD fd = openat(dirFd, path.path().c_str(), O_RDONLY | O_SYMLINK | O_CLOEXEC);
    if (!fd)
        throw SysError("opening %s to clear flags", PathFmt(path.path()));
    if (fchflags(fd.get(), 0)) {
        if (errno != ENOTSUP)
            throw SysError("clearing flags of path %s", PathFmt(path.path()));
    }
#endif
    auto st = fstatat(dirFd, path);

    /* Really make sure that the path is of a supported type. */
    if (!(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)))
        throw Error("file %s has an unsupported type", PathFmt(path.path()));

#if NIX_SUPPORT_ACL
    stripXAttrsAt(dirFd, path, options.ignoredAcls);
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
            throw BuildError(
                BuildResult::Failure::OutputRejected, "invalid ownership on file %s", PathFmt(path.path()));
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
        if (fchownat(dirFd, path.path().c_str(), geteuid(), getegid(), AT_SYMLINK_NOFOLLOW) == -1)
            throw SysError("changing owner of %1% to %2%", PathFmt(path.path()), geteuid());
    }
#endif

    if (S_ISDIR(st.st_mode)) {
        AutoCloseFD childDirFd = openFileEnsureBeneathNoSymlinks(
            dirFd,
            path,
#ifdef _WIN32
            FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            FILE_DIRECTORY_FILE
#else
            O_RDONLY | O_DIRECTORY | O_CLOEXEC
#endif
        );
        if (!childDirFd)
            throw NativeSysError("opening directory %s", PathFmt(path.path()));

        /* The iterator gets a duplicate, since it consumes the read
           position, while `childDirFd` is only used for `*At` calls. */
        for (auto & i : DirectoryIterator{dupDescriptor(childDirFd.get())}) {
            checkInterrupt();
            canonicalisePathMetaData_(childDirFd.get(), i.name, options, inodesSeen);
        }
    }
}

void canonicalisePathMetaData(
    Descriptor dirFd, const OsFilename & path, CanonicalizePathMetadataOptions options, InodesSeen & inodesSeen)
{
    canonicalisePathMetaData_(dirFd, path, options, inodesSeen);
}

void canonicalisePathMetaData(Descriptor dirFd, const OsFilename & path, CanonicalizePathMetadataOptions options)
{
    InodesSeen inodesSeen;
    canonicalisePathMetaData(dirFd, path, options, inodesSeen);
}

#ifndef _WIN32
static void setStoreWriteTime(Descriptor fd)
{
    struct ::timespec times[2] = {
        {.tv_sec = 0, .tv_nsec = UTIME_OMIT}, /* Leave alone. tv_sec is ignored. */
        {.tv_sec = mtimeStore, .tv_nsec = 0},
    };

    if (::futimens(fd, times) == -1)
        throw SysError([&]() {
            return HintFmt("changing modification time of %s (using `futimens`)", PathFmt(descriptorToPath(fd)));
        });
}
#endif

/* These hooks are supposed to be used only for freshly created files, not
   something we get from the builder output -- that needs additional validation.
   Use this for creating store objects with canonicalised permissions in a
   single restorePath pass. */
class CanonicalisingRestoreHooks : public RestoreSinkHooks
{
#if NIX_SUPPORT_ACL
    const StringSet & ignoredAcls;
#endif

    void canonicaliseTimestampAndPermissions(Descriptor fd, mode_t mode)
    {
#ifndef _WIN32 /* TODO: Figure out what sort of canonicalisation we even want. */
#  if NIX_SUPPORT_ACL
        stripXAttrs(fd, ignoredAcls);
#  endif
        if (::fchmod(fd, mode) == -1)
            throw SysError([&]() { return HintFmt("setting permissions on %s", PathFmt(descriptorToPath(fd))); });
        setStoreWriteTime(fd);
#endif
    }

    void anchor() override;

public:
#if NIX_SUPPORT_ACL
    explicit CanonicalisingRestoreHooks(const StringSet & ignoredAcls)
        : ignoredAcls(ignoredAcls)
    {
    }
#else
    CanonicalisingRestoreHooks() = default;
#endif

    void directoryDone(Descriptor dirFd) override
    {
        canonicaliseTimestampAndPermissions(dirFd, 0555);
    }

    void regularFileCreated(Descriptor fd, bool executable) override
    {
        canonicaliseTimestampAndPermissions(fd, executable ? 0555 : 0444);
    }

    void symlinkCreated(Descriptor parentFd, const OsFilename & name) override
    {
#ifndef _WIN32 /* Handling of symlinks on windows needs a ton more work. */
#  if NIX_SUPPORT_ACL
        stripXAttrsAt(parentFd, name, ignoredAcls);
#  endif
        struct ::timespec times[2] = {
            {.tv_sec = 0, .tv_nsec = UTIME_OMIT}, /* Leave alone. tv_sec is ignored. */
            {.tv_sec = mtimeStore, .tv_nsec = 0},
        };

        if (::utimensat(parentFd, name.c_str(), times, AT_SYMLINK_NOFOLLOW) == -1)
            throw SysError([&]() {
                return HintFmt(
                    "changing modification time of %s (using `utimensat`)",
                    PathFmt(descriptorToPath(parentFd) / name.path()));
            });
#endif
    }
};

void CanonicalisingRestoreHooks::anchor() {}

std::unique_ptr<RestoreSinkHooks> makeCanonicalisingRestoreHooks(NIX_WHEN_SUPPORT_ACLS2(const StringSet & ignoredAcls))
{
    return std::make_unique<CanonicalisingRestoreHooks>(NIX_WHEN_SUPPORT_ACLS2(ignoredAcls));
}

} // namespace nix
