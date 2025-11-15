// FIXME: detect in meson
#define HAVE_OPENAT2 1
#define HAVE_OPENAT 1

#include "util-config-private.hh"
#include "nix/util/signals.hh"
#include "nix/util/directory-source-accessor.hh"
#include "nix/util/posix-source-accessor.hh"

#if defined(__linux__) && HAVE_OPENAT2
#  include <sys/syscall.h>
#  include <linux/openat2.h>
#endif

#include <atomic>
#include <ranges>

namespace nix {

#if HAVE_OPENAT

namespace {

class DirFdSourceAccessor : public SourceAccessor
{
    /**
     * File descriptor of the root directory.
     */
    AutoCloseFD dirFd;

    /**
     * Path corresponding to the accessor.
     * @warning Do not use for any file operations!
     */
    std::filesystem::path root;

    /**
     * The most recent mtime seen by lstat(). This is a hack to
     * support dumpPathAndGetMtime(). Should remove this eventually.
     */
    time_t mtime = 0;

    bool trackLastModified = false;

    AutoCloseFD openFile(const CanonPath & path, int flags)
    {
        if (path.isRoot())
            return ::openat(dirFd.get(), ".", flags);

#  if defined(__linux__) && HAVE_OPENAT2
        /* Cache the result of whether openat2 is not supported. */
        static std::atomic_flag openat2Unsupported = ATOMIC_FLAG_INIT;

        if (!openat2Unsupported.test()) {
            /* No glibc wrapper yet, but there's a patch:
             * https://patchwork.sourceware.org/project/glibc/patch/20251029200519.3203914-1-adhemerval.zanella@linaro.org/
             */
            auto how = ::open_how{
                .flags = static_cast<decltype(::open_how::flags)>(flags),
                /* Symlinks are disallowed. RESOLVE_BENEATH is a bit overkill, since
                   CanonPath has the invariant of not having any `..` components, but
                   that's good practice anyway. */
                .resolve = RESOLVE_NO_SYMLINKS | RESOLVE_BENEATH,
            };

            auto res = ::syscall(__NR_openat2, dirFd.get(), path.rel_c_str(), &how, sizeof(how));
            if (res < 0 && errno == ENOSYS) {
                /* Cache that the syscall is not supported and fall through to openat. */
                openat2Unsupported.test_and_set();
            } else {
                return res;
            }
        }
#  endif

        AutoCloseFD parentFd;
        auto nrComponents = std::ranges::distance(path);
        auto components = std::views::take(path, nrComponents - 1); /* Everything but last component */
        auto getParentFd = [&]() { return parentFd ? parentFd.get() : dirFd.get(); };

        /* This rather convoluted loop is necessary to avoid TOCTOU when validating that
           no inner path component is a symlink. */
        for (auto it = components.begin(); it != components.end(); ++it) {
            std::string_view component = *it;
            parentFd = ::openat(
                getParentFd(),                  /* First iteration uses dirFd. */
                std::string(component).c_str(), /* Copy into a string to make NUL terminated. */
                O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC |
#  ifdef __linux__
                    O_PATH /* Linux-specific optimization. Files are open only for path resolution purposes. */
#  endif
            );

            if (!parentFd) {
                /* Construct the CanonPath for error message. */
                auto path2 = std::ranges::fold_left(components.begin(), ++it, CanonPath::root, [](auto lhs, auto rhs) {
                    lhs.push(rhs);
                    return lhs;
                });

                if (errno == ELOOP)
                    /* Do not allow any simlinks in internal path components.
                       This is what is necessary to emulate it without RESOLVE_NO_SYMLINKS. */
                    throw Error("path '%s' is a symlink", showPath(path2));

                return AutoCloseFD();
            }
        }

        return ::openat(getParentFd(), std::string(path.baseName().value()).c_str(), flags);
    }

    std::optional<struct ::stat> maybeLstatImpl(const CanonPath & path)
    {
        std::optional<struct ::stat> st{std::in_place};

        if (path.isRoot()) {
            if (::fstat(dirFd.get(), &*st)) /* Is already open. */
                throw SysError("getting status of '%s'", showPath(path));
            return st;
        }

        auto parentPath = path.parent().value();
        auto fd = openFile(parentPath, O_PATH | O_NOFOLLOW | O_CLOEXEC | O_DIRECTORY);
        if (!fd) {
            if (errno == ENOENT || errno == ENOTDIR)
                return std::nullopt;
            throw SysError("opening parent path of '%s'", showPath(path));
        }

        /* Should have the same semantics as lstat on a path. */
        if (::fstatat(fd.get(), std::string(path.baseName().value()).c_str(), &*st, AT_SYMLINK_NOFOLLOW)) {
            if (errno == ENOENT || errno == ENOTDIR)
                st.reset();
            else
                throw SysError("getting status of '%s'", showPath(path));
        }

        /* The contract is that trackLastModified implies that the caller uses the accessor
           from a single thread. Thus this is not a CAS loop. */
        if (trackLastModified)
            mtime = std::max(mtime, st->st_mtime);

        return st;
    }

public:
    DirFdSourceAccessor(AutoCloseFD rootFd_, std::filesystem::path root_, bool trackLastModified)
        : dirFd(std::move(rootFd_))
        , root(std::move(root_))
        , trackLastModified(trackLastModified)
    {
        if (root != root.root_path()) /* Don't prefix root directory. */
            displayPrefix = root.string();
    }

    void readFile(const CanonPath & path, Sink & sink, std::function<void(uint64_t)> sizeCallback) override
    {
        auto fd = openFile(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);

        auto size = getFileSize(fd.get());
        sizeCallback(size);
        drainFD(fd.get(), sink, {.expectedSize = size});
    }

    bool pathExists(const CanonPath & path) override
    {
        return maybeLstatImpl(path).has_value();
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        // TODO: Deduplicate with PosixSourceAccessor.

        auto st = maybeLstatImpl(path);
        if (!st)
            return std::nullopt;

        /* The contract is that trackLastModified implies that the caller uses the accessor
           from a single thread. Thus this is not a CAS loop. */
        if (trackLastModified)
            mtime = std::max(mtime, st->st_mtime);

        return PosixSourceAccessor::makeStat(*st);
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        auto fd = openFile(path, O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (!fd)
            throw SysError("opening directory '%1%'", path);

        auto dir = AutoCloseDir{::fdopendir(fd.get())};
        if (!dir)
            throw SysError("opening directory '%1%'", path);

        fd.release();

        DirEntries entries;
        struct dirent * dirent;

        while (errno = 0, dirent = ::readdir(dir.get())) {
            checkInterrupt();
            std::string name = dirent->d_name;
            if (name == "." || name == "..")
                continue;

            auto type = [&]() -> std::optional<Type> {
                switch (dirent->d_type) {
                case DT_REG:
                    return tRegular;
                case DT_DIR:
                    return tDirectory;
                case DT_LNK:
                    return tSymlink;
                case DT_BLK:
                    return tBlock;
                case DT_CHR:
                    return tChar;
                case DT_FIFO:
                    return tFifo;
                case DT_SOCK:
                    return tSocket;
                case DT_UNKNOWN:
                default:
                    return std::nullopt;
                }
            }();

            entries.emplace(std::move(name), type);
        }

        if (errno)
            throw SysError("reading directory '%1%'", showPath(path));

        return entries;
    }

    std::string readLink(const CanonPath & path) override
    {
        if (path.isRoot())
            throw Error("file '%s' is not a symbolic link", path);

        auto parentPath = path.parent().value();
        auto basename = std::string(path.baseName().value());

        auto parentFd = openFile(
            parentPath,
            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC |
#  ifdef __linux__
                O_PATH
#  endif
        );
        if (!parentFd)
            throw SysError("opening path '%1%'", showPath(parentPath));

        std::string target;
        target.resize(PATH_MAX);
        auto len = ::readlinkat(parentFd.get(), basename.c_str(), target.data(), target.size());
        if (len < 0)
            throw SysError("reading link '%1%'", showPath(path));
        target.resize(len);
        return target;
    }

    std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path) override
    {
        return root / path.rel();
    }
};

} // namespace

#endif

ref<SourceAccessor> makeDirectorySourceAccessor(AutoCloseFD fd, std::filesystem::path root, bool trackLastModified)
{
#if HAVE_OPENAT
    return make_ref<DirFdSourceAccessor>(std::move(fd), std::move(root), trackLastModified);
#else
    return make_ref<PosixSourceAccessor>(std::move(root), trackLastModified);
#endif
}

} // namespace nix
