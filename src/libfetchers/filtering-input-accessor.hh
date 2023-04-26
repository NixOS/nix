#pragma once

#include "input-accessor.hh"

namespace nix {

struct FilteringInputAccessor : InputAccessor
{
    ref<InputAccessor> next;
    CanonPath prefix;

    FilteringInputAccessor(const SourcePath & src)
        : next(src.accessor)
        , prefix(src.path)
    {
    }

    std::string readFile(const CanonPath & path) override;

    bool pathExists(const CanonPath & path) override;

    Stat lstat(const CanonPath & path) override;

    DirEntries readDirectory(const CanonPath & path) override;

    std::string readLink(const CanonPath & path) override;

    void checkAccess(const CanonPath & path);

    virtual bool isAllowed(const CanonPath & path) = 0;

    std::string showPath(const CanonPath & path) override;
};

struct CachingFilteringInputAccessor : FilteringInputAccessor
{
    std::map<CanonPath, bool> cache;

    using FilteringInputAccessor::FilteringInputAccessor;

    bool isAllowed(const CanonPath & path) override;

    virtual bool isAllowedUncached(const CanonPath & path) = 0;
};

}
