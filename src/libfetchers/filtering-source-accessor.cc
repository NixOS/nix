#include "filtering-source-accessor.hh"

namespace nix {

std::optional<std::filesystem::path> FilteringSourceAccessor::getPhysicalPath(const CanonPath & path)
{
    checkAccess(path);
    return next->getPhysicalPath(prefix / path);
}

std::string FilteringSourceAccessor::readFile(const CanonPath & path)
{
    checkAccess(path);
    return next->readFile(prefix / path);
}

bool FilteringSourceAccessor::pathExists(const CanonPath & path)
{
    return isAllowed(path) && next->pathExists(prefix / path);
}

std::optional<SourceAccessor::Stat> FilteringSourceAccessor::maybeLstat(const CanonPath & path)
{
    checkAccess(path);
    return next->maybeLstat(prefix / path);
}

SourceAccessor::DirEntries FilteringSourceAccessor::readDirectory(const CanonPath & path)
{
    checkAccess(path);
    DirEntries entries;
    for (auto & entry : next->readDirectory(prefix / path)) {
        if (isAllowed(path / entry.first))
            entries.insert(std::move(entry));
    }
    return entries;
}

std::string FilteringSourceAccessor::readLink(const CanonPath & path)
{
    checkAccess(path);
    return next->readLink(prefix / path);
}

std::string FilteringSourceAccessor::showPath(const CanonPath & path)
{
    return displayPrefix + next->showPath(prefix / path) + displaySuffix;
}

void FilteringSourceAccessor::checkAccess(const CanonPath & path)
{
    if (!isAllowed(path))
        throw makeNotAllowedError
            ? makeNotAllowedError(path)
            : RestrictedPathError("access to path '%s' is forbidden", showPath(path));
}

struct AllowListSourceAccessorImpl : AllowListSourceAccessor
{
    std::set<CanonPath> allowedPrefixes;

    AllowListSourceAccessorImpl(
        ref<SourceAccessor> next,
        std::set<CanonPath> && allowedPrefixes,
        MakeNotAllowedError && makeNotAllowedError)
        : AllowListSourceAccessor(SourcePath(next), std::move(makeNotAllowedError))
        , allowedPrefixes(std::move(allowedPrefixes))
    { }

    bool isAllowed(const CanonPath & path) override
    {
        return path.isAllowed(allowedPrefixes);
    }

    void allowPrefix(CanonPath prefix) override
    {
        allowedPrefixes.insert(std::move(prefix));
    }
};

ref<AllowListSourceAccessor> AllowListSourceAccessor::create(
    ref<SourceAccessor> next,
    std::set<CanonPath> && allowedPrefixes,
    MakeNotAllowedError && makeNotAllowedError)
{
    return make_ref<AllowListSourceAccessorImpl>(next, std::move(allowedPrefixes), std::move(makeNotAllowedError));
}

bool CachingFilteringSourceAccessor::isAllowed(const CanonPath & path)
{
    auto i = cache.find(path);
    if (i != cache.end()) return i->second;
    auto res = isAllowedUncached(path);
    cache.emplace(path, res);
    return res;
}

}
