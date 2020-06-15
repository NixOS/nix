#pragma once

#include "ref.hh"
#include "nar-info.hh"

namespace nix {

class NarInfoDiskCache
{
public:
    typedef enum { oValid, oInvalid, oUnknown } Outcome;

    virtual ~NarInfoDiskCache() { }

    virtual void createCache(std::string_view uri, PathView storeDir,
        bool wantMassQuery, int priority) = 0;

    struct CacheInfo
    {
        bool wantMassQuery;
        int priority;
    };

    virtual std::optional<CacheInfo> cacheExists(std::string_view uri) = 0;

    virtual std::pair<Outcome, std::shared_ptr<NarInfo>> lookupNarInfo(
        std::string_view uri, std::string_view hashPart) = 0;

    virtual void upsertNarInfo(
        std::string_view uri, std::string_view hashPart,
        std::shared_ptr<const ValidPathInfo> info) = 0;
};

/* Return a singleton cache object that can be used concurrently by
   multiple threads. */
ref<NarInfoDiskCache> getNarInfoDiskCache();

}
