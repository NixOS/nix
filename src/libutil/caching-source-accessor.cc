#include "nix/util/source-accessor.hh"

#include <boost/unordered/concurrent_flat_map.hpp>

namespace nix {

class CachingSourceAccessor : public SourceAccessor
{
    ref<SourceAccessor> next;

    boost::concurrent_flat_map<CanonPath, Stat> lstatCache;
    boost::concurrent_flat_map<CanonPath, std::string> readLinkCache;

public:
    CachingSourceAccessor(ref<SourceAccessor> next_)
        : next(std::move(next_))
    {
        displayPrefix.clear();
    }

    void readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback) override
    {
        next->readFile(path, sink, sizeCallback);
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        if (auto res = getConcurrent(lstatCache, path))
            return *res;

        auto st = next->maybeLstat(path);
        if (!st)
            return std::nullopt;

        /* Never evict, the evaluator better keep positive lookups cached. */
        lstatCache.emplace(path, *st);
        return st;
    }

    Stat lstat(const CanonPath & path) override
    {
        if (auto res = getConcurrent(lstatCache, path))
            return *res;

        auto st = next->lstat(path);
        /* Never evict, the evaluator better keep positive lookups cached. */
        lstatCache.emplace(path, st);
        return st;
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        return next->readDirectory(path);
    }

    void readDirectory(
        const CanonPath & dirPath,
        std::function<void(SourceAccessor & subdirAccessor, const CanonPath & subdirRelPath)> callback) override
    {
        return next->readDirectory(dirPath, std::move(callback));
    }

    std::string readLink(const CanonPath & path) override
    {
        if (auto res = getConcurrent(readLinkCache, path))
            return *res;

        auto target = next->readLink(path);
        /* Never evict, the evaluator better keep positive lookups cached. */
        readLinkCache.emplace(path, target);
        return target;
    }

    std::string showPath(const CanonPath & path) override
    {
        return next->showPath(path);
    }

    void invalidateCache() override
    {
        lstatCache.clear();
        readLinkCache.clear();
        next->invalidateCache();
    }

    std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path) override
    {
        return next->getPhysicalPath(path);
    }

    std::pair<CanonPath, std::optional<std::string>> getFingerprint(const CanonPath & path) override
    {
        return next->getFingerprint(path);
    }
};

ref<SourceAccessor> makeCachingSourceAccessor(ref<SourceAccessor> next)
{
    return make_ref<CachingSourceAccessor>(std::move(next));
}

} // namespace nix
