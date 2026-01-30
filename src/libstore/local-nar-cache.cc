#include "nix/util/nar-cache.hh"
#include "nix/util/file-system.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/fs-sink.hh"
#include "nix/store/pathlocks.hh"

#include <nlohmann/json.hpp>
#include <optional>

namespace nix {

namespace {

/**
 * NAR cache with local disk storage (private implementation).
 *
 * Uses file locks to ensure only one process downloads a NAR at a time.
 */
class LocalNarCache : public NarCache
{
    RestoreSink cacheSink;

public:

    LocalNarCache(std::filesystem::path cacheDir)
        : cacheSink(false)
    {
        createDirs(cacheDir);
        cacheSink.dstPath = std::move(cacheDir);
    }

    ref<NarAccessor> getOrInsert(const Hash & narHash, std::function<void(Sink &)> populate) override;
};

} // anonymous namespace

ref<NarAccessor> LocalNarCache::getOrInsert(const Hash & narHash, std::function<void(Sink &)> populate)
{
    // Check in-memory cache first
    if (auto * accessor = get(nars, narHash))
        return *accessor;

    auto cacheAccessor = [&](ref<NarAccessor> accessor) {
        nars.emplace(narHash, accessor);
        return accessor;
    };

    auto makeCacheFile = [&](const std::string & ext) -> CanonPath {
        return {narHash.to_string(HashFormat::Nix32, false) + "." + ext};
    };

    auto cacheFile = makeCacheFile("nar");
    auto listingFile = makeCacheFile("ls");
    auto lockFile = makeCacheFile("lock");

    auto cacheFilePath = cacheSink.dstPath / cacheFile.rel();
    auto lockFilePath = cacheSink.dstPath / lockFile.rel();
    auto listingFilePath = cacheSink.dstPath / listingFile.rel();

    // Helper to try loading from cache files using FD operations to avoid race conditions
    auto tryLoadFromCache = [&]() -> std::optional<ref<NarAccessor>> {
        try {
            // Try to open cache file - will throw if doesn't exist
            AutoCloseFD cacheFD = openFileReadonly(cacheFilePath);

            // Try lazy accessor with listing file first
            try {
                AutoCloseFD listingFD = openFileReadonly(listingFilePath);
                auto listingContent = readFile(listingFD.get());
                return cacheAccessor(makeLazyNarAccessor(
                    nlohmann::json::parse(listingContent).template get<NarListing>(),
                    seekableGetNarBytes(cacheFilePath)));
            } catch (SystemError &) {
                // Listing file missing or invalid, fall back to full NAR
            }

            // Fall back to reading full NAR
            auto narContent = readFile(cacheFD.get());
            return cacheAccessor(makeNarAccessor(std::move(narContent)));
        } catch (SystemError &) {
            // Cache file doesn't exist or can't be opened
            return std::nullopt;
        }
    };

    // Check if already cached (before acquiring lock)
    if (auto accessor = tryLoadFromCache())
        return *accessor;

    // Acquire lock to ensure only one process downloads this NAR
    AutoCloseFD lockFD = openLockFile(lockFilePath, true);
    FdLock lock(lockFD.get(), ltWrite, true, "waiting for NAR cache lock");

    // Check again after acquiring lock (another process might have just finished)
    if (auto accessor = tryLoadFromCache())
        return *accessor;

    // Download and cache the NAR
    NarListing listing;
    try {
        /* FIXME: do this asynchronously. */
        cacheSink.createRegularFile(cacheFile, [&](CreateRegularFileSink & fileSink) {
            auto source = sinkToSource([&](Sink & parseSink) {
                TeeSink teeSink{fileSink, parseSink};
                populate(teeSink);
            });
            listing = parseNarListing(*source);
        });
    } catch (...) {
        ignoreExceptionExceptInterrupt();
        StringSink narSink;
        populate(narSink);
        return cacheAccessor(makeNarAccessor(std::move(narSink.s)));
    }

    try {
        cacheSink.createRegularFile(listingFile, [&](CreateRegularFileSink & sink) {
            auto s = nlohmann::json(listing).dump();
            StringSource source{s};
            source.drainInto(sink);
        });
    } catch (...) {
        ignoreExceptionExceptInterrupt();
    }

    return cacheAccessor(makeLazyNarAccessor(std::move(listing), seekableGetNarBytes(cacheFilePath)));
}

std::unique_ptr<NarCache> makeLocalNarCache(std::filesystem::path cacheDir)
{
    return std::make_unique<LocalNarCache>(std::move(cacheDir));
}

} // namespace nix
