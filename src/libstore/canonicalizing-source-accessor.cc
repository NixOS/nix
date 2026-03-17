#include "nix/store/canonicalizing-source-accessor.hh"
#include "nix/util/file-system.hh"

namespace nix {

CanonicalizingSourceAccessor::CanonicalizingSourceAccessor(
    ref<SourceAccessor> inner, CanonicalizePathMetadataOptions options, InodesSeen & inodesSeen)
    : inner(std::move(inner))
    , options(options)
    , inodesSeen(inodesSeen)
{
}

std::optional<SourceAccessor::Stat> CanonicalizingSourceAccessor::maybeLstat(const CanonPath & path)
{
    auto physPath = inner->getPhysicalPath(path);
    if (!physPath)
        return inner->maybeLstat(path);

    auto st = nix::maybeLstat(physPath->c_str());
    if (!st)
        return std::nullopt;

    // Canonicalize this file on disk (chmod, lchown, utimes, xattr, UID check)
    canonicaliseOneFile(*physPath, *st, options, inodesSeen);

    // Return the simplified SourceAccessor::Stat.
    // isExecutable is derived from original S_IXUSR (preserved by canonicalize).
    Stat ret;
    if (S_ISREG(st->st_mode))
        ret.type = tRegular;
    else if (S_ISDIR(st->st_mode))
        ret.type = tDirectory;
    else if (S_ISLNK(st->st_mode))
        ret.type = tSymlink;
    else
        ret.type = tUnknown;
    ret.fileSize = S_ISREG(st->st_mode) ? std::optional<uint64_t>(st->st_size) : std::nullopt;
    ret.isExecutable = S_ISREG(st->st_mode) && (st->st_mode & S_IXUSR);
    return ret;
}

void CanonicalizingSourceAccessor::readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback)
{
    // File was already canonicalized during lstat() — just delegate the read
    inner->readFile(path, sink, sizeCallback);
}

SourceAccessor::DirEntries CanonicalizingSourceAccessor::readDirectory(const CanonPath & path)
{
    return inner->readDirectory(path);
}

std::string CanonicalizingSourceAccessor::readLink(const CanonPath & path)
{
    return inner->readLink(path);
}

std::optional<std::filesystem::path> CanonicalizingSourceAccessor::getPhysicalPath(const CanonPath & path)
{
    return inner->getPhysicalPath(path);
}

} // namespace nix
