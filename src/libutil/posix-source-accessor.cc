#include "posix-source-accessor.hh"
#include "file-system.hh"
#include "source-path.hh"
#include "signals.hh"
#include "sync.hh"

#include <chrono>
#include <filesystem>
#include <unordered_map>

namespace nix {

PosixSourceAccessor::PosixSourceAccessor(std::filesystem::path && root)
    : root(std::move(root))
{
    assert(root.empty() || root.is_absolute());
    displayPrefix = root.string();
}

PosixSourceAccessor::PosixSourceAccessor()
    : PosixSourceAccessor(std::filesystem::path {})
{ }

SourcePath PosixSourceAccessor::createAtRoot(const std::filesystem::path & path)
{
    std::filesystem::path path2 = absPath(path.string());
    return {
        make_ref<PosixSourceAccessor>(path2.root_path()),
        CanonPath { path2.relative_path().string() },
    };
}

std::filesystem::path PosixSourceAccessor::makeAbsPath(const CanonPath & path)
{
    return root.empty()
        ? (std::filesystem::path { path.abs() })
        : path.isRoot()
        ? /* Don't append a slash for the root of the accessor, since
             it can be a non-directory (e.g. in the case of `fetchTree
             { type = "file" }`). */
          root
        : root / path.rel();
}

void PosixSourceAccessor::readFile(
    const CanonPath & path,
    Sink & sink,
    std::function<void(uint64_t)> sizeCallback)
{
    assertNoSymlinks(path);

    auto ap = makeAbsPath(path);

    AutoCloseFD fd = toDescriptor(open(ap.string().c_str(), O_RDONLY
    #ifndef _WIN32
        | O_NOFOLLOW | O_CLOEXEC
    #endif
        ));
    if (!fd)
        throw SysError("opening file '%1%'", ap.string());

    struct stat st;
    if (fstat(fromDescriptorReadOnly(fd.get()), &st) == -1)
        throw SysError("statting file");

    sizeCallback(st.st_size);

    off_t left = st.st_size;

    std::array<unsigned char, 64 * 1024> buf;
    while (left) {
        checkInterrupt();
        ssize_t rd = read(fromDescriptorReadOnly(fd.get()), buf.data(), (size_t) std::min(left, (off_t) buf.size()));
        if (rd == -1) {
            if (errno != EINTR)
                throw SysError("reading from file '%s'", showPath(path));
        }
        else if (rd == 0)
            throw SysError("unexpected end-of-file reading '%s'", showPath(path));
        else {
            assert(rd <= left);
            sink({(char *) buf.data(), (size_t) rd});
            left -= rd;
        }
    }
}

bool PosixSourceAccessor::pathExists(const CanonPath & path)
{
    if (auto parent = path.parent()) assertNoSymlinks(*parent);
    return nix::pathExists(makeAbsPath(path).string());
}

std::optional<std::filesystem::file_status> PosixSourceAccessor::cachedLstat(const CanonPath & path)
{
    static SharedSync<std::unordered_map<Path, std::optional<std::filesystem::file_status>>> _cache;

    // Note: we convert std::filesystem::path to Path because the
    // former is not hashable on libc++.
    Path absPath = makeAbsPath(path).string();

    {
        auto cache(_cache.read());
        auto i = cache->find(absPath);
        if (i != cache->end()) return i->second;
    }

    auto st = nix::maybeSymlinkStat(absPath.c_str());

    auto cache(_cache.lock());
    if (cache->size() >= 16384) cache->clear();
    cache->emplace(absPath, st);

    return st;
}

std::optional<SourceAccessor::Stat> PosixSourceAccessor::maybeLstat(const CanonPath & path)
{
    if (auto parent = path.parent()) assertNoSymlinks(*parent);
    auto st = cachedLstat(path);
    if (!st) return std::nullopt;
    // TODO: last_write_time to compile things. This follows symlinks which we don't want
    auto st_time = std::chrono::time_point_cast<std::chrono::seconds>(std::filesystem::last_write_time(path.abs()));
    mtime = std::max(mtime, st_time.time_since_epoch().count());
    return Stat {
        .type =
            std::filesystem::is_regular_file(*st) ? tRegular :
            std::filesystem::is_directory(*st) ? tDirectory :
            std::filesystem::is_symlink(*st) ? tSymlink :
            tMisc,
        .fileSize = std::filesystem::is_regular_file(*st) ? std::optional<uint64_t>(std::filesystem::file_size(path.abs())) : std::nullopt,
        .isExecutable = std::filesystem::is_regular_file(*st) && st->permissions() == std::filesystem::perms::owner_exec,
    };
}

SourceAccessor::DirEntries PosixSourceAccessor::readDirectory(const CanonPath & path)
{
    assertNoSymlinks(path);
    DirEntries res;
    for (auto & entry : std::filesystem::directory_iterator{makeAbsPath(path)}) {
        checkInterrupt();
        auto type = [&]() -> std::optional<Type> {
            std::filesystem::file_type nativeType;
            try {
                nativeType = entry.symlink_status().type();
            } catch (std::filesystem::filesystem_error & e) {
                // We cannot always stat the child. (Ideally there is no
                // stat because the native directory entry has the type
                // already, but this isn't always the case.)
                if (e.code() == std::errc::permission_denied || e.code() == std::errc::operation_not_permitted)
                    return std::nullopt;
                else throw;
            }

            // cannot exhaustively enumerate because implementation-specific
            // additional file types are allowed.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
            switch (nativeType) {
            case std::filesystem::file_type::regular: return Type::tRegular; break;
            case std::filesystem::file_type::symlink: return Type::tSymlink; break;
            case std::filesystem::file_type::directory: return Type::tDirectory; break;
            default: return tMisc;
            }
#pragma GCC diagnostic pop
        }();
        res.emplace(entry.path().filename().string(), type);
    }
    return res;
}

std::string PosixSourceAccessor::readLink(const CanonPath & path)
{
    if (auto parent = path.parent()) assertNoSymlinks(*parent);
    return nix::readLink(makeAbsPath(path).string());
}

std::optional<std::filesystem::path> PosixSourceAccessor::getPhysicalPath(const CanonPath & path)
{
    return makeAbsPath(path);
}

void PosixSourceAccessor::assertNoSymlinks(CanonPath path)
{
    while (!path.isRoot()) {
        auto st = cachedLstat(path);
        if (st && std::filesystem::is_symlink(*st))
            throw Error("path '%s' is a symlink", showPath(path));
        path.pop();
    }
}

ref<SourceAccessor> getFSSourceAccessor()
{
    static auto rootFS = make_ref<PosixSourceAccessor>();
    return rootFS;
}

ref<SourceAccessor> makeFSSourceAccessor(std::filesystem::path root)
{
    return make_ref<PosixSourceAccessor>(std::move(root));
}
}
