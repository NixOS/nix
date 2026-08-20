#include "nix/store/posix-fs-canonicalise.hh"
#include "nix/store/build-result.hh"
#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"
#include "nix/store/store-api.hh"

#if NIX_SUPPORT_ACL
#  include <sys/xattr.h>
#endif

namespace nix {

const time_t mtimeStore = 1; /* 1 second into the epoch */

static void canonicaliseTimestampAndPermissions(const std::filesystem::path & path, const PosixStat & st)
{
    if (!S_ISLNK(st.st_mode)) {

        /* Mask out all type related bits. */
        mode_t mode = st.st_mode & ~S_IFMT;
        bool isDir = S_ISDIR(st.st_mode);
        if ((mode != 0444 || isDir) && mode != 0555) {
            mode = (st.st_mode & S_IFMT) | 0444 | (st.st_mode & S_IXUSR || isDir ? 0111 : 0);
            chmod(path, mode);
        }
    }

#ifndef _WIN32 // TODO implement
    if (st.st_mtime != mtimeStore) {
        PosixStat st2 = st;
        st2.st_mtime = mtimeStore, setWriteTime(path, st2);
    }
#endif
}

void canonicaliseTimestampAndPermissions(const std::filesystem::path & path)
{
    canonicaliseTimestampAndPermissions(path, lstat(path));
}

#if NIX_SUPPORT_ACL

static ssize_t listXAttr(const std::filesystem::path & path, char * list, size_t size, FinalSymlink finalSymlink)
{
    if (finalSymlink == FinalSymlink::DontFollow)
        return ::llistxattr(path.c_str(), list, size);
    return ::listxattr(path.c_str(), list, size);
}

static ssize_t listXAttr(Descriptor fd, char * list, size_t size, [[maybe_unused]] FinalSymlink finalSymlink)
{
    return ::flistxattr(fd, list, size);
}

static int removeXAttr(const std::filesystem::path & path, const char * name, FinalSymlink finalSymlink)
{
    if (finalSymlink == FinalSymlink::DontFollow)
        return ::lremovexattr(path.c_str(), name);
    return ::removexattr(path.c_str(), name);
}

static int removeXAttr(Descriptor fd, const char * name, [[maybe_unused]] FinalSymlink finalSymlink)
{
    return ::fremovexattr(fd, name);
}

static std::filesystem::path renderXAttrsFuncArgumentPath(Descriptor fd)
{
    return descriptorToPath(fd);
}

static std::filesystem::path renderXAttrsFuncArgumentPath(std::filesystem::path path)
{
    return path;
}

template<typename T>
static void
stripXAttrs(const T & pathOrFd, const StringSet & ignoredAcls, FinalSymlink finalSymlink = FinalSymlink::DontFollow)
{
    ssize_t eaSize = listXAttr(pathOrFd, nullptr, 0, finalSymlink);

    if (eaSize < 0) {
        if (errno != ENOTSUP && errno != ENODATA)
            throw SysError([&]() {
                return HintFmt("querying extended attributes of %s", PathFmt(renderXAttrsFuncArgumentPath(pathOrFd)));
            });
    } else if (eaSize > 0) {
        std::vector<char> eaBuf(eaSize);

        if ((eaSize = listXAttr(pathOrFd, eaBuf.data(), eaBuf.size(), finalSymlink)) < 0)
            throw SysError([&]() {
                return HintFmt("querying extended attributes of %s", PathFmt(renderXAttrsFuncArgumentPath(pathOrFd)));
            });

        using namespace std::string_view_literals;
        for (auto & eaName : tokenizeString<Strings>(std::string_view(eaBuf.data(), eaSize), "\0"sv)) {
            if (ignoredAcls.count(eaName))
                continue;
            if (removeXAttr(pathOrFd, eaName.c_str(), finalSymlink) == -1)
                throw SysError([&]() {
                    return HintFmt(
                        "removing extended attribute '%s' from %s",
                        eaName,
                        PathFmt(renderXAttrsFuncArgumentPath(pathOrFd)));
                });
        }
    }
}

#endif

static void canonicalisePathMetaData_(
    const std::filesystem::path & path, CanonicalizePathMetadataOptions options, InodesSeen & inodesSeen)
{
    checkInterrupt();

#ifdef __APPLE__
    /* Remove flags, in particular UF_IMMUTABLE which would prevent
       the file from being garbage-collected. FIXME: Use
       setattrlist() to remove other attributes as well. */
    if (lchflags(path.c_str(), 0)) {
        if (errno != ENOTSUP)
            throw SysError("clearing flags of path %1%", PathFmt(path));
    }
#endif

    auto st = lstat(path);

    /* Really make sure that the path is of a supported type. */
    if (!(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)))
        throw Error("file %1% has an unsupported type", PathFmt(path));

#if NIX_SUPPORT_ACL
    stripXAttrs(path, options.ignoredAcls);
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
            throw BuildError(BuildResult::Failure::OutputRejected, "invalid ownership on file %1%", PathFmt(path));
        mode_t mode = st.st_mode & ~S_IFMT;
        assert(
            S_ISLNK(st.st_mode)
            || (st.st_uid == geteuid() && (mode == 0444 || mode == 0555) && st.st_mtime == mtimeStore));
        return;
    }
#endif

    inodesSeen.insert(Inode(st.st_dev, st.st_ino));

    canonicaliseTimestampAndPermissions(path, st);

#ifndef _WIN32
    /* Change ownership to the current uid. */
    if (st.st_uid != geteuid()) {
        if (lchown(path.c_str(), geteuid(), getegid()) == -1)
            throw SysError("changing owner of %1% to %2%", PathFmt(path), geteuid());
    }
#endif

    if (S_ISDIR(st.st_mode)) {
        for (auto & i : DirectoryIterator{path}) {
            checkInterrupt();
            canonicalisePathMetaData_(i.path(), options, inodesSeen);
        }
    }
}

void canonicalisePathMetaData(
    const std::filesystem::path & path, CanonicalizePathMetadataOptions options, InodesSeen & inodesSeen)
{
    canonicalisePathMetaData_(path, options, inodesSeen);
}

void canonicalisePathMetaData(const std::filesystem::path & path, CanonicalizePathMetadataOptions options)
{
    InodesSeen inodesSeen;
    canonicalisePathMetaData_(path, options, inodesSeen);
}

} // namespace nix
