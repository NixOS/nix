#include <nlohmann/json.hpp>
#include "nix/store/remote-fs-accessor.hh"
#include "nix/util/nar-accessor.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace nix {

RemoteFSAccessor::RemoteFSAccessor(
    ref<Store> store, bool requireValidPath, std::optional<std::filesystem::path> cacheDir_)
    : store(store)
    , requireValidPath(requireValidPath)
    , cacheDir(std::move(cacheDir_))
{
    if (cacheDir)
        createDirs(*cacheDir);
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

    if (cacheDir) {
        auto makeCacheFile = [&](const std::string & ext) {
            auto res = *cacheDir / info->narHash.to_string(HashFormat::Nix32, false);
            res += ".";
            res += ext;
            return res;
        };

        auto cacheFile = makeCacheFile("nar");
        auto listingFile = makeCacheFile("ls");

        if (nix::pathExists(cacheFile)) {
            try {
                auto listing = nix::readFile(listingFile);
                auto listingJson = nlohmann::json::parse(listing);
                return cacheAccessor(makeLazyNarAccessor(listingJson, seekableGetNarBytes(cacheFile)));
            } catch (SystemError &) {
            }

            try {
                return cacheAccessor(makeNarAccessor(nix::readFile(cacheFile)));
            } catch (SystemError &) {
            }
        }

        auto nar = getNar();

        try {
            /* FIXME: do this asynchronously. */
            writeFile(cacheFile, nar);
        } catch (...) {
            ignoreExceptionExceptInterrupt();
        }

        auto narAccessor = makeNarAccessor(std::move(nar));

        try {
            nlohmann::json j = listNarDeep(*narAccessor, CanonPath::root);
            writeFile(listingFile, j.dump());
        } catch (...) {
            ignoreExceptionExceptInterrupt();
        }

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

std::string RemoteFSAccessor::readFile(const CanonPath & path)
{
    auto res = fetch(path);
    return res.first->readFile(res.second);
}

std::string RemoteFSAccessor::readLink(const CanonPath & path)
{
    auto res = fetch(path);
    return res.first->readLink(res.second);
}

} // namespace nix
