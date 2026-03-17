#pragma once

#include "nix/util/source-accessor.hh"
#include "nix/store/posix-fs-canonicalise.hh"

namespace nix {

/**
 * A SourceAccessor decorator that canonicalizes each file's metadata
 * (permissions, ownership, timestamps, xattrs) during lstat(), before
 * the file is read. This folds the canonicalize pass into the NAR
 * dump traversal, eliminating a separate tree walk.
 */
struct CanonicalizingSourceAccessor : SourceAccessor
{
    ref<SourceAccessor> inner;
    CanonicalizePathMetadataOptions options;
    InodesSeen & inodesSeen;

    CanonicalizingSourceAccessor(
        ref<SourceAccessor> inner, CanonicalizePathMetadataOptions options, InodesSeen & inodesSeen);

    std::optional<Stat> maybeLstat(const CanonPath & path) override;
    void readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback) override;
    DirEntries readDirectory(const CanonPath & path) override;
    std::string readLink(const CanonPath & path) override;
    std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path) override;
};

} // namespace nix
