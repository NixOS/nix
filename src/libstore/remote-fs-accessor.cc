#include "nix/store/remote-fs-accessor.hh"

namespace nix {

void RemoteFSAccessor::anchor() {}

RemoteFSAccessor::RemoteFSAccessor(ref<Store> store, bool, std::optional<AbsolutePath> cacheDir)
    : store(store)
    , narCache(cacheDir)
{
}

std::pair<ref<SourceAccessor>, CanonPath> RemoteFSAccessor::fetch(const CanonPath & path)
{
    auto [storePath, restPath] = store->toStorePath(store->storeDir + path.abs());
    auto accessor = accessObject(storePath);
    if (!accessor)
        throw InvalidPath("path '%1%' is not a valid store path", store->printStorePath(storePath));
    return {ref{std::move(accessor)}, restPath};
}

std::shared_ptr<SourceAccessor> RemoteFSAccessor::accessObject(const StorePath & storePath)
{
    // Check if we already have the NAR hash for this store path
    if (auto * narHash = get(narHashes, storePath.hashPart()))
        return narCache.getOrInsert(*narHash, [&](Sink & sink) { store->narFromPath(storePath, sink); });

    std::shared_ptr<const ValidPathInfo> info;
    try {
        info = store->queryPathInfo(storePath);
    } catch (InvalidPath &) {
        return nullptr;
    }

    // Cache the mapping from store path to NAR hash
    narHashes.emplace(storePath.hashPart(), info->narHash);

    // Get or create the NAR accessor
    return narCache.getOrInsert(info->narHash, [&](Sink & sink) { store->narFromPath(storePath, sink); });
}

std::optional<SourceAccessor::Stat> RemoteFSAccessor::maybeLstat(const CanonPath & path)
{
    if (path.isRoot())
        return Stat{.type = tDirectory};
    /* FIXME: Correctly handle invalid names (return nullopt). */
    auto [storePath, restPath] = store->toStorePath(store->storeDir + path.abs());
    auto accessor = accessObject(storePath);
    if (!accessor)
        return std::nullopt;
    return accessor->maybeLstat(restPath);
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
