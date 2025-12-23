#include "nix/util/memory-source-accessor.hh"
#include "nix/util/source-accessor.hh"
#include "nix/util/unix-source-accessor.hh"

namespace nix {

using namespace nix::unix;

std::optional<SourceAccessor::Stat> UnixFileSourceAccessor::maybeLstat(const CanonPath & path)
{
    if (!path.isRoot())
        /* This is not a directory. Nothing can be beneath it. */
        return std::nullopt;

    std::call_once(statFlag, [this] {
        if (::fstat(fd.get(), &cachedStat) == -1)
            throw SysError("statting file '%s'", displayPrefix);
        updateMtime(cachedStat.st_mtime);
    });

    return posixStatToAccessorStat(cachedStat);
}

void UnixFileSourceAccessor::readFile(const CanonPath & path, Sink & sink, std::function<void(uint64_t)> sizeCallback)
{
    if (!path.isRoot())
        throw FileNotFound("path '%s' does not exist", showPath(path));

    struct ::stat st;
    /* Fresh fstat. TODO: Maybe reuse the cached stat? There are some nuances when it comes
       to non-regular file handling (e.g. /dev/stdin) that is also system dependent.
       See https://github.com/NixOS/nix/issues/9330. We should probably ban non-regular files
       completely. */
    if (::fstat(fd.get(), &st) == -1)
        throw SysError("getting status of '%s'", showPath(path));

    off_t left = st.st_size;
    off_t offset = 0;
    /* Currently trusts st_size to be correct, errors out if EOF is reached before reading st_size bytes:
       https://github.com/NixOS/nix/issues/10667. */
    sizeCallback(left);

    /* TODO: Optimise for the case when Sink is an FdSink and call sendfile. Can
       also use copy_file_range to leverage reflinking if the destination is a
       regular file and not a socket. */

    std::array<unsigned char, 64 * 1024> buf;
    while (left) {
        checkInterrupt();
        /* N.B. Using pread for thread-safety. File pointer must not be modified. */
        ssize_t rd = ::pread(fd.get(), buf.data(), std::min<std::size_t>(left, buf.size()), offset);
        if (rd == -1) {
            if (errno != EINTR)
                throw SysError("reading from file '%s'", showPath(path));
        } else if (rd == 0)
            throw SysError("unexpected end-of-file reading '%s'", showPath(path));
        else {
            assert(rd <= left);
            sink({reinterpret_cast<char *>(buf.data()), static_cast<std::size_t>(rd)});
            left -= rd;
            offset += rd;
        }
    }
}

std::function<void(AutoCloseFD, CanonPath)> UnixDirectorySourceAccessor::makeDirFdCallback()
{
    if (!dirFdCache)
        return nullptr;

    return [this](AutoCloseFD fd, CanonPath key) {
        auto cache(dirFdCache->lock());
        assert(fd);
        cache->upsert(key, make_ref<AutoCloseFD>(std::move(fd)));
    };
}

std::pair<Descriptor, std::shared_ptr<AutoCloseFD>> UnixDirectorySourceAccessor::openParent(const CanonPath & path)
{
    assert(!path.isRoot());
    auto parent = path.parent().value();
    if (parent.isRoot())
        return {fd.get(), nullptr};

    if (dirFdCache) {
        if (auto cachedFd = dirFdCache->lock()->get(parent)) {
            assert((*cachedFd)->get());
            return {(*cachedFd)->get(), *cachedFd};
        }
    }

    AutoCloseFD parentFdOwning = openFileEnsureBeneathNoSymlinks(
        fd.get(), parent, O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC, 0, makeDirFdCallback());
    if (!parentFdOwning && (errno == ELOOP || errno == ENOTDIR))
        throw SymlinkNotAllowed(parent);
    return {parentFdOwning.get(), make_ref<AutoCloseFD>(std::move(parentFdOwning))};
}

std::optional<SourceAccessor::Stat> UnixDirectorySourceAccessor::maybeLstat(const CanonPath & path)
try {
    struct ::stat st;

    if (path.isRoot()) {
        /* This error is unexpected. Would only happen if the directory fd is messed up. */
        if (::fstat(fd.get(), &st) == -1)
            throw SysError("getting status of '%s'", showPath(path));
    } else {
        auto [parentFd, parentFdOwning] = openParent(path);
        if (parentFd == INVALID_DESCRIPTOR)
            return std::nullopt;
        if (::fstatat(parentFd, std::string(path.baseName().value()).c_str(), &st, AT_SYMLINK_NOFOLLOW) == -1)
            return std::nullopt;

        if (dirFdCache && parentFdOwning) {
            assert(*parentFdOwning);
            dirFdCache->lock()->upsert(path.parent().value(), ref<AutoCloseFD>(parentFdOwning));
        }
    }

    updateMtime(st.st_mtime);
    return posixStatToAccessorStat(st);
} catch (SymlinkNotAllowed & e) {
    throw SymlinkNotAllowed(e.path, "path '%s' is a symlink", showPath(e.path));
}

void UnixDirectorySourceAccessor::readFile(
    const CanonPath & path, Sink & sink, std::function<void(uint64_t)> sizeCallback)
try {
    if (path.isRoot())
        throw NotARegularFile("'%s' is not a regular file", showPath(path));

    AutoCloseFD fileFd = openFileEnsureBeneathNoSymlinks(fd.get(), path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (!fileFd) {
        if (errno == ELOOP) /* The last component is a symlink. */
            throw NotARegularFile("'%s' is a symlink, not a regular file", showPath(path));
        if (errno == ENOENT || errno == ENOTDIR) /* Intermediate component might not exist. */
            throw FileNotFound("file '%s' does not exist", showPath(path));
        throw SysError("opening '%s'", showPath(path));
    }

    UnixFileSourceAccessor fileAccessor(std::move(fileFd), rootPath / path, trackLastModified);
    fileAccessor.readFile(CanonPath::root, sink, sizeCallback);

    if (auto fileMtime = fileAccessor.getLastModified())
        mtime = std::max(mtime, *fileMtime);
} catch (SymlinkNotAllowed & e) {
    throw SymlinkNotAllowed(e.path, "path '%s' is a symlink", showPath(e.path));
}

SourceAccessor::DirEntries UnixDirectorySourceAccessor::readDirectory(const CanonPath & path)
try {
    AutoCloseFD dirFdOwning;

    if (path.isRoot()) {
        /* Get a fresh file descriptor for thread-safety. */
        dirFdOwning = ::openat(fd.get(), ".", O_DIRECTORY | O_RDONLY | O_CLOEXEC);
        if (!dirFdOwning)
            throw SysError("opening directory '%s'", showPath(path));
    } else {
        dirFdOwning = openFileEnsureBeneathNoSymlinks(fd.get(), path, O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (!dirFdOwning) {
            if (errno == ENOTDIR)
                throw NotADirectory("'%s' is not a directory", showPath(path));
            throw SysError("opening directory '%s'", showPath(path));
        }
    }

    AutoCloseDir dir(::fdopendir(dirFdOwning.get()));
    if (!dir)
        throw SysError("opening directory '%s'", showPath(path));
    dirFdOwning.release();

    DirEntries entries;
    const ::dirent * dirent = nullptr;

    while (errno = 0, dirent = ::readdir(dir.get())) {
        checkInterrupt();
        std::string_view name(dirent->d_name);
        if (name == "." || name == "..")
            continue;

        std::optional<Type> type;
        switch (dirent->d_type) {
        case DT_REG:
            type = tRegular;
            break;
        case DT_DIR:
            type = tDirectory;
            break;
        case DT_LNK:
            type = tSymlink;
            break;
        case DT_CHR:
            type = tChar;
            break;
        case DT_BLK:
            type = tBlock;
            break;
        case DT_FIFO:
            type = tFifo;
            break;
        case DT_SOCK:
            type = tSocket;
            break;
        default:
            type = std::nullopt;
            break;
        }
        entries.emplace(name, type);
    }

    if (errno)
        throw SysError("reading directory %1%", path);

    return entries;
} catch (SymlinkNotAllowed & e) {
    throw SymlinkNotAllowed(e.path, "path '%s' is a symlink", showPath(e.path));
}

std::string UnixDirectorySourceAccessor::readLink(const CanonPath & path)
try {
    if (path.isRoot())
        throw NotASymlink("file '%s' is not a symlink", showPath(path));

    auto [parentFd, parentFdOwning] = openParent(path);
    if (parentFd == INVALID_DESCRIPTOR)
        throw FileNotFound("file '%s' does not exist", showPath(path));

    if (dirFdCache && parentFdOwning) {
        assert(*parentFdOwning);
        dirFdCache->lock()->upsert(path.parent().value(), ref<AutoCloseFD>(std::move(parentFdOwning)));
    }

    try {
        return readLinkAt(parentFd, CanonPath(path.baseName().value()));
    } catch (SysError & e) {
        if (e.errNo == EINVAL)
            throw NotASymlink("file '%s' is not a symlink", showPath(path));
        throw;
    }
} catch (SymlinkNotAllowed & e) {
    throw SymlinkNotAllowed(e.path, "path '%s' is a symlink", showPath(e.path));
}

namespace {

class SymlinkSourceAccessor : public MemorySourceAccessor
{
    bool trackLastModified;
    std::time_t mtime;
    CanonPath rootPath;

public:
    SymlinkSourceAccessor(std::string target, CanonPath rootPath_, bool trackLastModified, std::time_t mtime)
        : trackLastModified(trackLastModified)
        , mtime(mtime)
        , rootPath(std::move(rootPath_))
    {
        MemorySink sink{*this};
        sink.createSymlink(CanonPath::root, target);
        displayPrefix = rootPath.abs();
    }

    std::optional<std::time_t> getLastModified() override
    {
        return trackLastModified ? std::optional{mtime} : std::nullopt;
    }

    std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path) override
    {
        auto fsRootPath = std::filesystem::path(rootPath.abs());
        if (path.isRoot())
            return fsRootPath;
        return fsRootPath / path.rel(); /* RHS must be a relative path. */
    }

    std::string showPath(const CanonPath & path) override
    {
        /* When rendering the file itself omit the trailing slash. */
        return path.isRoot() ? displayPrefix : SourceAccessor::showPath(path);
    }
};

} // namespace

ref<SourceAccessor> getFSSourceAccessor()
{
    static auto rootFS =
        make_ref<UnixDirectorySourceAccessor>(openDirectory("/"), CanonPath("/"), /*trackLastModified=*/false);
    return rootFS;
}

ref<SourceAccessor> makeFSSourceAccessor(std::filesystem::path root, bool trackLastModified)
{
    using namespace unix;

    if (root.empty())
        return getFSSourceAccessor();

    assert(root.is_absolute());
    auto rootPath = CanonPath(root.native());
    assert(rootPath.abs().starts_with("/")); /* In case the invariant is broken somehow. */

    /* Any symlinks get resolved eagerly here. Unlike with SourceAccessor semantics that requires that
       all links get resolved manually, the root can be resolved eagerly. */
    AutoCloseFD fd(::open(rootPath.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));

    if (!fd) {
        if (errno == ELOOP) /* Opening a symlink, can read it straight into memory source accessor. */ {
            auto parent = rootPath.parent().value(); /* Always present, isRoot is handled above. */
            auto name = std::string(rootPath.baseName().value());
            AutoCloseFD parentFd(::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
            if (!parentFd)
                throw SysError("opening '%s'", parent.abs());

            struct ::stat st;
            if (::fstatat(parentFd.get(), name.c_str(), &st, AT_SYMLINK_NOFOLLOW) == -1)
                throw SysError("statting '%s' relative to parent directory '%s'", name, parent.abs());

            auto target = readLinkAt(parentFd.get(), CanonPath(name));
            return make_ref<SymlinkSourceAccessor>(
                std::move(target), std::move(rootPath), trackLastModified, st.st_mtime);
        }

        throw SysError("opening '%s'", rootPath.abs());
    }

    struct ::stat st;
    if (::fstat(fd.get(), &st) == -1)
        throw SysError("statting '%s'", rootPath.abs());

    if (S_ISDIR(st.st_mode))
        return make_ref<UnixDirectorySourceAccessor>(std::move(fd), std::move(rootPath), trackLastModified, /*dirFdCacheSize=*/0);

    /* TODO: Ban non-regular files that cannot file represented by the FSO
       semantics. See comment in UnixFileSourceAccessor::readFile. */
    return make_ref<UnixFileSourceAccessor>(std::move(fd), std::move(rootPath), trackLastModified, &st);
}

} // namespace nix
