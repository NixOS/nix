#include "filtering-input-accessor.hh"

namespace nix {

std::string FilteringInputAccessor::readFile(const CanonPath & path)
{
    checkAccess(path);
    return next->readFile(prefix + path);
}

bool FilteringInputAccessor::pathExists(const CanonPath & path)
{
    return isAllowed(path) && next->pathExists(prefix + path);
}

InputAccessor::Stat FilteringInputAccessor::lstat(const CanonPath & path)
{
    checkAccess(path);
    return next->lstat(prefix + path);
}

InputAccessor::DirEntries FilteringInputAccessor::readDirectory(const CanonPath & path)
{
    checkAccess(path);
    DirEntries entries;
    for (auto & entry : next->readDirectory(prefix + path)) {
        if (isAllowed(path + entry.first))
            entries.insert(std::move(entry));
    }
    return entries;
}

std::string FilteringInputAccessor::readLink(const CanonPath & path)
{
    checkAccess(path);
    return next->readLink(prefix + path);
}

void FilteringInputAccessor::checkAccess(const CanonPath & path)
{
    if (!isAllowed(path))
        throw Error("access to path '%s' has been filtered out", showPath(path));
}

std::string FilteringInputAccessor::showPath(const CanonPath & path)
{
    return next->showPath(prefix + path);
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
