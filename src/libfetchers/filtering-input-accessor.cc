#include "filtering-input-accessor.hh"

#include <unordered_set>

namespace nix {

std::string FilteringInputAccessor::readFile(const CanonPath & path)
{
    checkAccess(path);
    return next->readFile(prefix / path);
}

bool FilteringInputAccessor::pathExists(const CanonPath & path)
{
    return isAllowed(path) && next->pathExists(prefix / path);
}

SourceAccessor::Stat FilteringInputAccessor::lstat(const CanonPath & path)
{
    checkAccess(path);
    return next->lstat(prefix / path);
}

std::optional<InputAccessor::Stat> FilteringInputAccessor::maybeLstat(const CanonPath & path)
{
    return isAllowed(path) ? next->maybeLstat(prefix / path) : std::nullopt;
}

InputAccessor::DirEntries FilteringInputAccessor::readDirectory(const CanonPath & path)
{
    checkAccess(path);
    DirEntries entries;
    for (auto & entry : next->readDirectory(prefix / path)) {
        if (isAllowed(path / entry.first))
            entries.insert(std::move(entry));
    }
    return entries;
}

std::string FilteringInputAccessor::readLink(const CanonPath & path)
{
    checkAccess(path);
    return next->readLink(prefix / path);
}

std::string FilteringInputAccessor::showPath(const CanonPath & path)
{
    return next->showPath(prefix / path);
}

void FilteringInputAccessor::checkAccess(const CanonPath & path)
{
    if (!isAllowed(path))
        throw makeNotAllowedError
            ? makeNotAllowedError(path)
            : RestrictedPathError("access to path '%s' is forbidden", showPath(path));
}

struct AllowListInputAccessorImpl : AllowListInputAccessor
{
    std::unordered_set<CanonPath> allowedPrefixes;
    std::unordered_set<CanonPath> allowedPaths;

    AllowListInputAccessorImpl(
        ref<InputAccessor> next,
        std::unordered_set<CanonPath> && allowedPrefixes,
        std::unordered_set<CanonPath> && allowedPaths,
        MakeNotAllowedError && makeNotAllowedError)
        : AllowListInputAccessor(SourcePath(next), std::move(makeNotAllowedError))
        , allowedPrefixes(std::move(allowedPrefixes))
        , allowedPaths(std::move(allowedPaths))
    { }

    bool isAllowed(const CanonPath & path) override
    {
        return allowedPaths.count(path) || path.isAllowed(allowedPrefixes);
    }

    void allowPrefix(CanonPath prefix) override
    {
        allowedPrefixes.insert(std::move(prefix));
    }

    void allowPath(CanonPath path) override
    {
        allowedPaths.insert(std::move(path));
    }
};

ref<AllowListInputAccessor> AllowListInputAccessor::create(
    ref<InputAccessor> next,
    std::unordered_set<CanonPath> && allowedPrefixes,
    std::unordered_set<CanonPath> && allowedPaths,
    MakeNotAllowedError && makeNotAllowedError)
{
    return make_ref<AllowListInputAccessorImpl>(
        next,
        std::move(allowedPrefixes),
        std::move(allowedPaths),
        std::move(makeNotAllowedError));
}

bool CachingFilteringInputAccessor::isAllowed(const CanonPath & path)
{
    auto i = cache.find(path);
    if (i != cache.end()) return i->second;
    auto res = isAllowedUncached(path);
    cache.emplace(path, res);
    return res;
}

}
