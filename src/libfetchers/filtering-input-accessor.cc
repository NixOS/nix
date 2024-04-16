#include "filtering-input-accessor.hh"

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

std::optional<InputAccessor::Stat> FilteringInputAccessor::maybeLstat(const CanonPath & path)
{
    checkAccess(path);
    return next->maybeLstat(prefix / path);
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
    return displayPrefix + next->showPath(prefix / path) + displaySuffix;
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
    std::set<CanonPath> allowedPrefixes;

    AllowListInputAccessorImpl(
        ref<InputAccessor> next,
        std::set<CanonPath> && allowedPrefixes,
        MakeNotAllowedError && makeNotAllowedError)
        : AllowListInputAccessor(SourcePath(next), std::move(makeNotAllowedError))
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

ref<AllowListInputAccessor> AllowListInputAccessor::create(
    ref<InputAccessor> next,
    std::set<CanonPath> && allowedPrefixes,
    MakeNotAllowedError && makeNotAllowedError)
{
    return make_ref<AllowListInputAccessorImpl>(next, std::move(allowedPrefixes), std::move(makeNotAllowedError));
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
