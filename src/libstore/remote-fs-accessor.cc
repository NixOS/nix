#include "nix/store/remote-fs-accessor.hh"

namespace nix {

RemoteFSAccessor::RemoteFSAccessor(ref<Store> store, bool requireValidPath, std::optional<AbsolutePath> cacheDir)
    : store(store)
    , narCache(cacheDir)
    , requireValidPath(requireValidPath)
{
}

std::pair<ref<SourceAccessor>, CanonPath> RemoteFSAccessor::fetch(const CanonPath & path)
{
    auto [storePath, restPath] = store->toStorePath(store->storeDir + path.abs());
    if (requireValidPath && !store->isValidPath(storePath))
        throw InvalidPath("path '%1%' is not a valid store path", store->printStorePath(storePath));
    return {ref{accessObject(storePath)}, restPath};
}

std::shared_ptr<SourceAccessor> RemoteFSAccessor::accessObject(const StorePath & storePath)
{
    // Check if we already have the NAR hash for this store path
    if (auto * narHash = get(narHashes, storePath.hashPart()))
        return narCache.getOrInsert(*narHash, [&](Sink & sink) { store->narFromPath(storePath, sink); });

    // Query the path info to get the NAR hash
    auto info = store->queryPathInfo(storePath);

    // Cache the mapping from store path to NAR hash
    narHashes.emplace(storePath.hashPart(), info->narHash);

    // Get or create the NAR accessor
    return narCache.getOrInsert(info->narHash, [&](Sink & sink) { store->narFromPath(storePath, sink); });
}

std::optional<SourceAccessor::Stat> RemoteFSAccessor::maybeLstat(const CanonPath & path)
{
    auto res = fetch(path);
    return res.first->maybeLstat(res.second);
}

SourceAccessor::DirEntries RemoteFSAccessor::readDirectory(const CanonPath & path)
{
    auto res = fetch(path);
    return res.first->readDirectory(res.second);
}

void RemoteFSAccessor::readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback)
{
    auto res = fetch(path);
    res.first->readFile(res.second, sink, sizeCallback);
}

std::string RemoteFSAccessor::readLink(const CanonPath & path)
{
    auto res = fetch(path);
    return res.first->readLink(res.second);
}

} // namespace nix
