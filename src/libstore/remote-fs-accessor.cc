#include <nlohmann/json.hpp>
#include "nix/store/remote-fs-accessor.hh"
#include "nix/store/nar-cache.hh"

namespace nix {

RemoteFSAccessor::RemoteFSAccessor(ref<Store> store, bool requireValidPath, std::shared_ptr<NarCache> narCache)
    : store(store)
    , requireValidPath(requireValidPath)
    , narCache(std::move(narCache))
{
}

std::pair<ref<SourceAccessor>, CanonPath> RemoteFSAccessor::fetch(const CanonPath & path)
{
    auto [storePath, restPath] = store->toStorePath(store->storeDir + path.abs());
    if (requireValidPath && !store->isValidPath(storePath))
        throw InvalidPath("path '%1%' is not a valid store path", store->printStorePath(storePath));
    return {ref{accessObject(storePath)}, CanonPath{restPath}};
}

std::shared_ptr<SourceAccessor> RemoteFSAccessor::accessObject(const StorePath & storePath)
{
    if (auto * narHash = get(narHashes, storePath.hashPart())) {
        if (auto * accessor = get(nars, *narHash))
            return *accessor;
    }

    auto info = store->queryPathInfo(storePath);

    auto cacheAccessor = [&](ref<SourceAccessor> accessor) {
        narHashes.emplace(storePath.hashPart(), info->narHash);
        nars.emplace(info->narHash, accessor);
        return accessor;
    };

    auto getNar = [&]() {
        StringSink sink;
        store->narFromPath(storePath, sink);
        return std::move(sink.s);
    };

    if (narCache) {
        if (auto listingData = narCache->getNarListing(info->narHash))
            return cacheAccessor(makeLazyNarAccessor(
                nlohmann::json::parse(*listingData).template get<NarListing>(), narCache->getNarBytes(info->narHash)));

        if (auto nar = narCache->getNar(info->narHash))
            return cacheAccessor(makeNarAccessor(std::move(*nar)));

        auto nar = getNar();

        StringSource source{nar};
        narCache->upsertNar(info->narHash, source);

        auto narAccessor = makeNarAccessor(std::move(nar));

        narCache->upsertNarListing(info->narHash, nlohmann::json(narAccessor->getListing()).dump());

        return cacheAccessor(narAccessor);
    }

    return cacheAccessor(makeNarAccessor(getNar()));
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

void RemoteFSAccessor::readFile(const CanonPath & path, Sink & sink, std::function<void(uint64_t)> sizeCallback)
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
